// coup_game.cc -- OpenSpiel Coup implementation
// Pure C++, no dependency on C engine.

#include "coup_game.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <sstream>

namespace open_spiel {
namespace coup {

namespace {

const GameType kGameType{
    /*short_name=*/"coup",
    /*long_name=*/"Coup",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kExplicitStochastic,
    GameType::Information::kImperfectInformation,
    GameType::Utility::kGeneralSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/kMaxPlayers,
    /*min_num_players=*/kMinPlayers,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/true,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    {{"players", GameParameter(2)}},
};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::make_shared<CoupGame>(params);
}

REGISTER_SPIEL_GAME(kGameType, Factory);

const char* CardName(int type) {
  switch (type) {
    case kDuke: return "Duke";
    case kAssassin: return "Assassin";
    case kCaptain: return "Captain";
    case kAmbassador: return "Ambassador";
    case kContessa: return "Contessa";
    default: return "Unknown";
  }
}

}  // namespace

// ===========================================================================
// CoupGame
// ===========================================================================

CoupGame::CoupGame(const GameParameters& params)
    : Game(kGameType, params),
      num_players_(ParameterValue<int>("players")) {
  assert(num_players_ >= kMinPlayers && num_players_ <= kMaxPlayers);
}

std::unique_ptr<State> CoupGame::NewInitialState() const {
  return std::make_unique<CoupState>(shared_from_this());
}

// ===========================================================================
// CoupState -- Construction
// ===========================================================================

CoupState::CoupState(std::shared_ptr<const Game> game)
    : State(game) {
  int np = NumPlayers();
  // Initialize deck: 3 of each type.
  for (int i = 0; i < kNumCardTypes; i++) {
    deck_[i] = kCardsPerType;
  }
  // Initialize players.
  for (int i = 0; i < kMaxPlayers; i++) {
    players_[i].coins = kStartingCoins;
    players_[i].cards[0] = {0, true};
    players_[i].cards[1] = {0, true};
  }
  // Mark unused player slots as dead.
  for (int i = np; i < kMaxPlayers; i++) {
    players_[i].cards[0].alive = false;
    players_[i].cards[1].alive = false;
    players_[i].coins = 0;
  }
  // Start in DEAL phase.
  phase_ = kDeal;
  deal_count_ = 0;
  active_player_ = 0;
  turn_player_ = 0;
}

const CoupGame* CoupState::parent_game() const {
  return static_cast<const CoupGame*>(game_.get());
}

int CoupState::NumPlayers() const {
  return parent_game()->NumPlayers();
}

// ===========================================================================
// CoupState -- OpenSpiel interface
// ===========================================================================

Player CoupState::CurrentPlayer() const {
  if (IsTerminal()) return kTerminalPlayerId;
  if (phase_ == kDeal || phase_ == kChanceRedraw || phase_ == kChanceExchange) {
    return kChancePlayerId;
  }
  return active_player_;
}

bool CoupState::IsTerminal() const {
  return winner_ >= 0;
}

std::vector<double> CoupState::Returns() const {
  std::vector<double> returns(NumPlayers(), 0.0);
  if (winner_ >= 0) {
    for (int i = 0; i < NumPlayers(); i++) {
      returns[i] = (i == winner_) ? 1.0 : -1.0;
    }
  }
  return returns;
}

std::unique_ptr<State> CoupState::Clone() const {
  return std::make_unique<CoupState>(*this);
}

// ===========================================================================
// CoupState -- Chance outcomes
// ===========================================================================

std::vector<std::pair<Action, double>> CoupState::ChanceOutcomes() const {
  chance_outcomes_cache_.clear();
  assert(phase_ == kDeal || phase_ == kChanceRedraw ||
         phase_ == kChanceExchange);
  int total = DeckTotal();
  assert(total > 0);
  for (int i = 0; i < kNumCardTypes; i++) {
    if (deck_[i] > 0) {
      chance_outcomes_cache_.push_back({i, static_cast<double>(deck_[i]) / total});
    }
  }
  return chance_outcomes_cache_;
}

// ===========================================================================
// CoupState -- Legal actions
// ===========================================================================

std::vector<Action> CoupState::LegalActions() const {
  legal_actions_cache_.clear();
  if (IsTerminal()) return legal_actions_cache_;
  if (phase_ == kDeal || phase_ == kChanceRedraw ||
      phase_ == kChanceExchange) {
    for (int i = 0; i < kNumCardTypes; i++) {
      if (deck_[i] > 0) legal_actions_cache_.push_back(i);
    }
    return legal_actions_cache_;
  }
  switch (phase_) {
    case kMainAction: LegalActionsMainAction(); break;
    case kChallengeAction: LegalActionsChallengeAction(); break;
    case kBlock: LegalActionsBlock(); break;
    case kChallengeBlock: LegalActionsChallengeBlock(); break;
    case kLoseCard: LegalActionsLoseCard(); break;
    case kExchangeDiscard: LegalActionsExchangeDiscard(); break;
    default:
      assert(false);
      break;
  }
  return legal_actions_cache_;
}

void CoupState::LegalActionsMainAction() const {
  auto& actions = legal_actions_cache_;
  int np = NumPlayers();
  int coins = players_[active_player_].coins;

  // If 10+ coins, must coup.
  if (coins >= kForceCoupThreshold) {
    for (int t = 0; t < np; t++) {
      if (t != active_player_ && PlayerIsAlive(t)) {
        actions.push_back(kCoupPlayer0 + t);
      }
    }
    return;
  }

  // Income always available.
  actions.push_back(kIncome);
  // Foreign aid always available.
  actions.push_back(kForeignAid);
  // Tax (claim Duke).
  actions.push_back(kTax);
  // Exchange (claim Ambassador).
  actions.push_back(kExchange);

  // Targeted actions.
  for (int t = 0; t < np; t++) {
    if (t == active_player_ || !PlayerIsAlive(t)) continue;
    // Coup (need 7+ coins).
    if (coins >= kCoupCost) {
      actions.push_back(kCoupPlayer0 + t);
    }
    // Steal (claim Captain) -- target must be alive.
    actions.push_back(kStealPlayer0 + t);
    // Assassinate (claim Assassin, need 3+ coins).
    if (coins >= kAssassinateCost) {
      actions.push_back(kAssassinatePlayer0 + t);
    }
  }
}

void CoupState::LegalActionsChallengeAction() const {
  legal_actions_cache_.push_back(kChallenge);
  legal_actions_cache_.push_back(kPass);
}

void CoupState::LegalActionsBlock() const {
  auto& actions = legal_actions_cache_;
  int pa = pending_action_;

  // Pass is always available.
  actions.push_back(kPass);

  if (pa == kForeignAid) {
    actions.push_back(kBlockDuke);
  } else if (pa >= kStealPlayer0 && pa <= kStealPlayer0 + 5) {
    actions.push_back(kBlockCaptain);
    actions.push_back(kBlockAmbassador);
  } else if (pa >= kAssassinatePlayer0 && pa <= kAssassinatePlayer0 + 5) {
    actions.push_back(kBlockContessa);
  }
}

void CoupState::LegalActionsChallengeBlock() const {
  legal_actions_cache_.push_back(kChallenge);
  legal_actions_cache_.push_back(kPass);
}

void CoupState::LegalActionsLoseCard() const {
  const auto& p = players_[lose_card_player_];
  if (p.cards[0].alive) legal_actions_cache_.push_back(kDiscardSlot0);
  if (p.cards[1].alive) legal_actions_cache_.push_back(kDiscardSlot1);
}

void CoupState::LegalActionsExchangeDiscard() const {
  auto& actions = legal_actions_cache_;
  // The player has their own cards (slots 0,1) and exchange cards (slots 2,3).
  // Available slots: own alive cards + exchange cards that exist.
  // Collect which slots are available.
  bool slot_available[4] = {false, false, false, false};
  if (players_[turn_player_].cards[0].alive) slot_available[0] = true;
  if (players_[turn_player_].cards[1].alive) slot_available[1] = true;
  if (exchange_cards_[0] >= 0) slot_available[2] = true;
  if (exchange_cards_[1] >= 0) slot_available[3] = true;

  if (first_discard_ < 0) {
    // First discard: can pick any available slot, BUT must leave at least
    // one higher-indexed available slot for the second discard (canonical
    // ordering constraint).
    for (int s = 0; s < 4; s++) {
      if (!slot_available[s]) continue;
      // Check if there exists at least one available slot with index > s.
      bool has_higher = false;
      for (int t = s + 1; t < 4; t++) {
        if (slot_available[t]) { has_higher = true; break; }
      }
      if (has_higher) {
        actions.push_back(kDiscardSlot0 + s);
      }
    }
  } else {
    // Second discard: only slots with index strictly above first_discard_.
    for (int s = first_discard_ + 1; s < 4; s++) {
      if (slot_available[s]) {
        actions.push_back(kDiscardSlot0 + s);
      }
    }
  }
}

// ===========================================================================
// CoupState -- DoApplyAction
// ===========================================================================

void CoupState::DoApplyAction(Action action) {
  switch (phase_) {
    case kDeal:
      AdvanceDeal(action);
      break;
    case kChanceRedraw:
      // Replace the revealed card with a new draw from deck.
      {
        // The claimant's card was shuffled back already. Now draw replacement.
        // Find which card slot of the claimant to replace.
        // The claimant had revealed a card during challenge defense. That card
        // was shuffled back. Now assign the new card.
        int claimant = challenge_on_block_ ? blocker_ : turn_player_;
        // Find the dead slot that we just revealed and need to replace.
        // During challenge defense: the revealed card was marked alive still
        // (it was shuffled back), so we need to find the slot that had the
        // claimed card. We stored which slot in claimed_card_.
        // Actually, let's track this properly. The card was already shuffled
        // back into the deck (done in ResolveChallenge). Now we just need to
        // assign the new drawn card to that slot.
        // The slot was the one with the claimed card type.
        bool assigned = false;
        for (int s = 0; s < kCardsPerPlayer; s++) {
          if (players_[claimant].cards[s].alive &&
              players_[claimant].cards[s].type == claimed_card_) {
            players_[claimant].cards[s].type = action;
            assigned = true;
            break;
          }
        }
        assert(assigned);
        DeckRemove(action);

        // Challenger loses influence.
        lose_card_player_ = challenger_;
        active_player_ = challenger_;
        phase_ = kLoseCard;
        if (challenge_on_block_) {
          // Block challenge failed (blocker had the card).
          // Challenger loses card, then block stands -> action cancelled.
          post_lose_card_phase_ = kResolve;
          action_blocked_ = true;
          needs_block_ = false;
        } else {
          // Action challenge failed (claimant had the card).
          // Challenger loses card, then action proceeds (possibly through block).
          post_lose_card_phase_ = kResolve;
          action_blocked_ = false;
          needs_block_ = IsBlockable(pending_action_);
        }
      }
      break;
    case kChanceExchange:
      // Draw a card for ambassador exchange.
      exchange_cards_[exchange_draw_count_] = action;
      DeckRemove(action);
      exchange_draw_count_++;
      if (exchange_draw_count_ >= 2) {
        // Both cards drawn. Move to exchange discard.
        phase_ = kExchangeDiscard;
        active_player_ = turn_player_;
        first_discard_ = -1;
      }
      // Otherwise stay in kChanceExchange for the second draw.
      break;
    case kMainAction:
      ApplyMainAction(action);
      break;
    case kChallengeAction:
      ApplyChallengeAction(action);
      break;
    case kBlock:
      ApplyBlock(action);
      break;
    case kChallengeBlock:
      ApplyChallengeBlock(action);
      break;
    case kLoseCard:
      ApplyLoseCard(action);
      break;
    case kExchangeDiscard:
      ApplyExchangeDiscard(action);
      break;
    case kResolve:
      // Should not happen -- resolve is automatic.
      assert(false);
      break;
  }
}

// ===========================================================================
// Phase: DEAL
// ===========================================================================

void CoupState::AdvanceDeal(int card_type) {
  int np = NumPlayers();
  int player_idx = deal_count_ / kCardsPerPlayer;
  int card_slot = deal_count_ % kCardsPerPlayer;

  players_[player_idx].cards[card_slot].type = card_type;
  players_[player_idx].cards[card_slot].alive = true;
  DeckRemove(card_type);
  deal_count_++;

  if (deal_count_ >= np * kCardsPerPlayer) {
    // Deal complete. Start main action phase.
    StartMainAction();
  }
  // Otherwise, stay in kDeal for next chance node.
}

// ===========================================================================
// Phase: MAIN_ACTION
// ===========================================================================

void CoupState::StartMainAction() {
  phase_ = kMainAction;
  active_player_ = turn_player_;
  pending_action_ = -1;
  responded_mask_ = 0;
  blocker_ = -1;
  block_card_ = -1;
  challenger_ = -1;
  claimed_card_ = -1;
  action_blocked_ = false;
  challenge_on_block_ = false;
  needs_block_ = false;
  exchange_cards_[0] = -1;
  exchange_cards_[1] = -1;
  exchange_draw_count_ = 0;
  first_discard_ = -1;

  // Pre-set responded_mask for dead players and turn_player.
  int np = NumPlayers();
  for (int i = 0; i < np; i++) {
    if (!PlayerIsAlive(i) || i == turn_player_) {
      responded_mask_ |= (1 << i);
    }
  }
}

void CoupState::ApplyMainAction(Action action) {
  pending_action_ = action;
  AddHistoryEntry(active_player_, action, kMainAction, 0);

  // Deduct assassination cost immediately.
  if (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5) {
    players_[turn_player_].coins -= kAssassinateCost;
  }

  // Unchallengeable, unblockable actions resolve immediately.
  if (action == kIncome) {
    // +1 coin, advance turn.
    players_[turn_player_].coins =
        std::min(players_[turn_player_].coins + 1, kMaxCoins);
    AdvanceTurn();
    return;
  }

  if (action >= kCoupPlayer0 && action <= kCoupPlayer0 + 5) {
    // Pay 7, target loses influence.
    players_[turn_player_].coins -= kCoupCost;
    int target = action - kCoupPlayer0;
    lose_card_player_ = target;
    active_player_ = target;
    phase_ = kLoseCard;
    post_lose_card_phase_ = kResolve;
    action_blocked_ = false;
    return;
  }

  // Challengeable actions: tax, exchange, steal, assassinate, foreign_aid.
  // Foreign aid is not challengeable but is blockable.
  if (action == kForeignAid) {
    // Foreign aid: not challengeable, but blockable by anyone claiming Duke.
    // Go to BLOCK phase -- cycle through other players.
    phase_ = kBlock;
    // Reset responded_mask: turn_player already set, dead players set.
    // Find first responder.
    active_player_ = NextResponder(turn_player_);
    if (responded_mask_ == ((1 << NumPlayers()) - 1)) {
      // No one can block (shouldn't happen in valid game).
      ApplyResolve();
    }
    return;
  }

  // Challengeable actions: tax, exchange, steal, assassinate.
  if (IsChallengeable(action)) {
    // Determine the claimed card.
    claimed_card_ = GetClaimedCard(action);
    // Go to CHALLENGE_ACTION phase. Cycle through opponents.
    phase_ = kChallengeAction;
    active_player_ = NextResponder(turn_player_);
    return;
  }

  // Should not reach here.
  assert(false);
}

// ===========================================================================
// Phase: CHALLENGE_ACTION
// ===========================================================================

void CoupState::ApplyChallengeAction(Action action) {
  AddHistoryEntry(active_player_, action, kChallengeAction, 0);

  if (action == kChallenge) {
    challenger_ = active_player_;
    // Check if claimant (turn_player_) actually has the claimed card.
    bool has_card = false;
    for (int s = 0; s < kCardsPerPlayer; s++) {
      if (players_[turn_player_].cards[s].alive &&
          players_[turn_player_].cards[s].type == claimed_card_) {
        has_card = true;
        break;
      }
    }

    if (has_card) {
      // Claimant has the card. Shuffle it back, draw replacement (chance node).
      // Challenger will lose influence after.
      challenge_on_block_ = false;
      // Shuffle the claimed card back into deck.
      DeckAdd(claimed_card_);
      // Move to chance node for redraw.
      phase_ = kChanceRedraw;
    } else {
      // Claimant doesn't have the card. Claimant loses influence.
      // Action is cancelled.
      lose_card_player_ = turn_player_;
      active_player_ = turn_player_;
      phase_ = kLoseCard;
      post_lose_card_phase_ = kResolve;
      action_blocked_ = true;  // Action doesn't go through.
    }
    return;
  }

  if (action == kPass) {
    // This player passes on challenging.
    responded_mask_ |= (1 << active_player_);
    // Check if all have responded.
    int full_mask = (1 << NumPlayers()) - 1;
    if ((responded_mask_ & full_mask) == full_mask) {
      // No one challenged. Move to block phase if blockable, else resolve.
      if (IsBlockable(pending_action_)) {
        phase_ = kBlock;
        // Reset responded mask for block phase.
        responded_mask_ = 0;
        int np = NumPlayers();
        for (int i = 0; i < np; i++) {
          if (!PlayerIsAlive(i)) {
            responded_mask_ |= (1 << i);
          }
        }
        // For steal/assassinate: only target can block.
        // For foreign aid: anyone except turn_player can block (handled above).
        int target = GetTarget(pending_action_);
        if (target >= 0) {
          // Only target can block steal/assassinate.
          for (int i = 0; i < np; i++) {
            if (i != target) {
              responded_mask_ |= (1 << i);
            }
          }
          // Check if target is still alive (might have died earlier).
          int full_mask2 = (1 << np) - 1;
          if ((responded_mask_ & full_mask2) == full_mask2) {
            // Target is dead; no one to block. Resolve.
            ApplyResolve();
            return;
          }
          active_player_ = target;
        } else {
          // Foreign aid -- shouldn't be here (handled in ApplyMainAction).
          assert(false);
        }
      } else {
        // Unchallengeable + unblockable? Tax and exchange are challengeable
        // but not blockable (well, exchange is just not blockable).
        // Tax and exchange: no block, resolve.
        ApplyResolve();
      }
    } else {
      // Next responder.
      active_player_ = NextResponder(active_player_);
    }
    return;
  }

  assert(false);
}

// ===========================================================================
// Phase: BLOCK
// ===========================================================================

void CoupState::ApplyBlock(Action action) {
  AddHistoryEntry(active_player_, action, kBlock, 0);

  if (action == kPass) {
    responded_mask_ |= (1 << active_player_);
    int full_mask = (1 << NumPlayers()) - 1;
    if ((responded_mask_ & full_mask) == full_mask) {
      // No one blocked. Resolve the action.
      ApplyResolve();
    } else {
      active_player_ = NextResponder(active_player_);
    }
    return;
  }

  // A block was declared.
  blocker_ = active_player_;
  if (action == kBlockContessa) {
    block_card_ = kContessa;
  } else if (action == kBlockCaptain) {
    block_card_ = kCaptain;
  } else if (action == kBlockAmbassador) {
    block_card_ = kAmbassador;
  } else if (action == kBlockDuke) {
    block_card_ = kDuke;
  } else {
    assert(false);
  }

  // The block can be challenged. Cycle through other players.
  phase_ = kChallengeBlock;
  responded_mask_ = 0;
  int np = NumPlayers();
  for (int i = 0; i < np; i++) {
    if (!PlayerIsAlive(i) || i == blocker_) {
      responded_mask_ |= (1 << i);
    }
  }
  active_player_ = NextResponder(blocker_);
  // If no one can challenge (all dead/responded), block stands.
  int full_mask = (1 << np) - 1;
  if ((responded_mask_ & full_mask) == full_mask) {
    // Block stands, action cancelled.
    action_blocked_ = true;
    ApplyResolve();
  }
}

// ===========================================================================
// Phase: CHALLENGE_BLOCK
// ===========================================================================

void CoupState::ApplyChallengeBlock(Action action) {
  AddHistoryEntry(active_player_, action, kChallengeBlock, 0);

  if (action == kChallenge) {
    challenger_ = active_player_;
    // Check if blocker has the block_card_.
    bool has_card = false;
    for (int s = 0; s < kCardsPerPlayer; s++) {
      if (players_[blocker_].cards[s].alive &&
          players_[blocker_].cards[s].type == block_card_) {
        has_card = true;
        break;
      }
    }

    if (has_card) {
      // Blocker has the card. Shuffle back, redraw. Challenger loses influence.
      challenge_on_block_ = true;
      claimed_card_ = block_card_;
      DeckAdd(block_card_);
      phase_ = kChanceRedraw;
    } else {
      // Blocker doesn't have it. Blocker loses influence. Block fails.
      // Action goes through after.
      lose_card_player_ = blocker_;
      active_player_ = blocker_;
      phase_ = kLoseCard;
      post_lose_card_phase_ = kResolve;
      action_blocked_ = false;  // Block failed, action proceeds.
    }
    return;
  }

  if (action == kPass) {
    responded_mask_ |= (1 << active_player_);
    int full_mask = (1 << NumPlayers()) - 1;
    if ((responded_mask_ & full_mask) == full_mask) {
      // No one challenged the block. Block stands.
      action_blocked_ = true;
      ApplyResolve();
    } else {
      active_player_ = NextResponder(active_player_);
    }
    return;
  }

  assert(false);
}

// ===========================================================================
// Phase: LOSE_CARD
// ===========================================================================

void CoupState::ApplyLoseCard(Action action) {
  AddHistoryEntry(lose_card_player_, action, kLoseCard, 0);

  int slot = action - kDiscardSlot0;  // 0 or 1
  assert(slot == 0 || slot == 1);
  assert(players_[lose_card_player_].cards[slot].alive);

  players_[lose_card_player_].cards[slot].alive = false;

  CheckGameOver();
  if (IsTerminal()) return;

  // Transition to the post-lose-card phase.
  if (needs_block_) {
    // After a failed action challenge, the action needs to go through BLOCK.
    needs_block_ = false;
    int target = GetTarget(pending_action_);
    if (target >= 0 && PlayerIsAlive(target)) {
      phase_ = kBlock;
      responded_mask_ = 0;
      int np = NumPlayers();
      for (int i = 0; i < np; i++) {
        if (!PlayerIsAlive(i) || i != target) {
          responded_mask_ |= (1 << i);
        }
      }
      active_player_ = target;
      return;
    }
    // Target is dead (killed by challenge?), resolve directly.
    ApplyResolve();
  } else {
    phase_ = post_lose_card_phase_;
    if (phase_ == kResolve) {
      ApplyResolve();
    }
  }
}

// ===========================================================================
// Phase: EXCHANGE_DISCARD
// ===========================================================================

void CoupState::ApplyExchangeDiscard(Action action) {
  AddHistoryEntry(turn_player_, action, kExchangeDiscard, 0);

  int slot = action - kDiscardSlot0;  // 0-3

  if (first_discard_ < 0) {
    // First discard.
    first_discard_ = slot;
    // Return the card from this slot to the deck.
    int card_type = -1;
    if (slot < 2) {
      card_type = players_[turn_player_].cards[slot].type;
      // Mark this slot as "discarded" -- we'll clean up after second discard.
    } else {
      card_type = exchange_cards_[slot - 2];
    }
    DeckAdd(card_type);

    // Always need a second discard (player keeps N alive cards, discards 2).
    // Stay in EXCHANGE_DISCARD phase for second pick.
    return;
  }

  // Second discard.
  int second_slot = slot;
  assert(second_slot > first_discard_);

  // Return this card to deck.
  int card_type = -1;
  if (second_slot < 2) {
    card_type = players_[turn_player_].cards[second_slot].type;
  } else {
    card_type = exchange_cards_[second_slot - 2];
  }
  DeckAdd(card_type);

  // Now assign the kept cards to the player's alive slots.
  // Collect the kept cards (slots NOT in {first_discard_, second_slot}).
  std::vector<int> kept_types;
  for (int s = 0; s < 4; s++) {
    if (s == first_discard_ || s == second_slot) continue;
    if (s < 2) {
      if (players_[turn_player_].cards[s].alive) {
        kept_types.push_back(players_[turn_player_].cards[s].type);
      }
    } else {
      if (exchange_cards_[s - 2] >= 0) {
        kept_types.push_back(exchange_cards_[s - 2]);
      }
    }
  }

  // Assign kept cards to alive slots.
  int ki = 0;
  for (int s = 0; s < kCardsPerPlayer; s++) {
    if (players_[turn_player_].cards[s].alive) {
      assert(ki < static_cast<int>(kept_types.size()));
      players_[turn_player_].cards[s].type = kept_types[ki++];
    }
  }

  // Clean up exchange state.
  exchange_cards_[0] = -1;
  exchange_cards_[1] = -1;
  first_discard_ = -1;

  // Exchange complete, advance turn.
  AdvanceTurn();
}

// ===========================================================================
// Phase: RESOLVE
// ===========================================================================

void CoupState::ApplyResolve() {
  // If action was blocked or cancelled (by failed challenge), just advance.
  if (action_blocked_) {
    AdvanceTurn();
    return;
  }

  int pa = pending_action_;

  if (pa == kForeignAid) {
    players_[turn_player_].coins =
        std::min(players_[turn_player_].coins + 2, kMaxCoins);
    AdvanceTurn();
  } else if (pa == kTax) {
    players_[turn_player_].coins =
        std::min(players_[turn_player_].coins + 3, kMaxCoins);
    AdvanceTurn();
  } else if (pa == kExchange) {
    // Draw 2 cards from deck (chance nodes).
    phase_ = kChanceExchange;
    exchange_draw_count_ = 0;
    exchange_cards_[0] = -1;
    exchange_cards_[1] = -1;
  } else if (pa >= kStealPlayer0 && pa <= kStealPlayer0 + 5) {
    int target = pa - kStealPlayer0;
    int stolen = std::min(2, players_[target].coins);
    players_[target].coins -= stolen;
    players_[turn_player_].coins =
        std::min(players_[turn_player_].coins + stolen, kMaxCoins);
    AdvanceTurn();
  } else if (pa >= kAssassinatePlayer0 && pa <= kAssassinatePlayer0 + 5) {
    // 3 coins already deducted. Target loses influence.
    int target = pa - kAssassinatePlayer0;
    if (PlayerIsAlive(target)) {
      lose_card_player_ = target;
      active_player_ = target;
      phase_ = kLoseCard;
      post_lose_card_phase_ = kResolve;
      // Set action_blocked_ to true so that when we come back to resolve
      // after lose_card, we just advance turn.
      action_blocked_ = true;
    } else {
      // Target already dead (from challenge?). Just advance.
      AdvanceTurn();
    }
  } else if (pa >= kCoupPlayer0 && pa <= kCoupPlayer0 + 5) {
    // Coup was already handled (lose_card before resolve). Advance turn.
    AdvanceTurn();
  } else if (pa == kIncome) {
    // Should have been resolved immediately.
    AdvanceTurn();
  } else {
    // Unknown action.
    assert(false);
  }
}

// ===========================================================================
// Helpers
// ===========================================================================

int CoupState::DeckTotal() const {
  int total = 0;
  for (int i = 0; i < kNumCardTypes; i++) total += deck_[i];
  return total;
}

void CoupState::DeckRemove(int card_type) {
  assert(card_type >= 0 && card_type < kNumCardTypes);
  assert(deck_[card_type] > 0);
  deck_[card_type]--;
}

void CoupState::DeckAdd(int card_type) {
  assert(card_type >= 0 && card_type < kNumCardTypes);
  deck_[card_type]++;
}

bool CoupState::PlayerIsAlive(int p) const {
  return players_[p].IsAlive();
}

int CoupState::CountAlivePlayers() const {
  int count = 0;
  for (int i = 0; i < NumPlayers(); i++) {
    if (PlayerIsAlive(i)) count++;
  }
  return count;
}

void CoupState::CheckGameOver() {
  int alive_count = 0;
  int last_alive = -1;
  for (int i = 0; i < NumPlayers(); i++) {
    if (PlayerIsAlive(i)) {
      alive_count++;
      last_alive = i;
    }
  }
  if (alive_count == 1) {
    winner_ = last_alive;
    return;
  }
  // Max turns tiebreaker: most alive cards, then most coins
  if (turn_count_ >= kMaxTurns && alive_count > 1) {
    int best = -1;
    int best_cards = -1, best_coins = -1;
    for (int i = 0; i < NumPlayers(); i++) {
      if (!PlayerIsAlive(i)) continue;
      int cards = players_[i].NumAliveCards();
      int coins = players_[i].coins;
      if (cards > best_cards || (cards == best_cards && coins > best_coins)) {
        best = i;
        best_cards = cards;
        best_coins = coins;
      }
    }
    winner_ = best;
  }
}

int CoupState::NextAlivePlayer(int from) const {
  int np = NumPlayers();
  int p = (from + 1) % np;
  while (p != from) {
    if (PlayerIsAlive(p)) return p;
    p = (p + 1) % np;
  }
  return from;  // only one alive
}

int CoupState::NextResponder(int from) const {
  int np = NumPlayers();
  int p = (from + 1) % np;
  int full_mask = (1 << np) - 1;
  while ((responded_mask_ & full_mask) != full_mask) {
    if (!(responded_mask_ & (1 << p))) {
      return p;
    }
    p = (p + 1) % np;
  }
  // All have responded -- shouldn't be called in this case.
  return -1;
}

void CoupState::AdvanceTurn() {
  turn_count_++;
  CheckGameOver();
  if (IsTerminal()) return;
  turn_player_ = NextAlivePlayer(turn_player_);
  StartMainAction();
}

int CoupState::GetTarget(int action) const {
  if (action >= kCoupPlayer0 && action <= kCoupPlayer0 + 5) {
    return action - kCoupPlayer0;
  }
  if (action >= kStealPlayer0 && action <= kStealPlayer0 + 5) {
    return action - kStealPlayer0;
  }
  if (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5) {
    return action - kAssassinatePlayer0;
  }
  return -1;  // No target.
}

int CoupState::GetClaimedCard(int action) const {
  if (action == kTax) return kDuke;
  if (action == kExchange) return kAmbassador;
  if (action >= kStealPlayer0 && action <= kStealPlayer0 + 5) return kCaptain;
  if (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5) {
    return kAssassin;
  }
  return -1;
}

bool CoupState::IsChallengeable(int action) const {
  return action == kTax || action == kExchange ||
         (action >= kStealPlayer0 && action <= kStealPlayer0 + 5) ||
         (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5);
}

bool CoupState::IsBlockable(int action) const {
  // Steal and assassinate are blockable by the target.
  // Foreign aid is blockable by anyone (but handled separately).
  return (action >= kStealPlayer0 && action <= kStealPlayer0 + 5) ||
         (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5);
}

void CoupState::AddHistoryEntry(int acting_player, int action, int phase,
                                int result) {
  history_buffer_.push_back({acting_player, action, phase, result});
}

// ===========================================================================
// CoupState -- ActionToString
// ===========================================================================

std::string CoupState::ActionToString(Player player, Action action) const {
  if (phase_ == kDeal || phase_ == kChanceRedraw ||
      phase_ == kChanceExchange) {
    return std::string("Deal:") + CardName(action);
  }
  switch (action) {
    case kIncome: return "Income";
    case kForeignAid: return "ForeignAid";
    case kTax: return "Tax";
    case kExchange: return "Exchange";
    case kChallenge: return "Challenge";
    case kPass: return "Pass";
    case kBlockContessa: return "BlockContessa";
    case kBlockCaptain: return "BlockCaptain";
    case kBlockAmbassador: return "BlockAmbassador";
    case kBlockDuke: return "BlockDuke";
    case kDiscardSlot0: return "DiscardSlot0";
    case kDiscardSlot1: return "DiscardSlot1";
    case kDiscardSlot2: return "DiscardSlot2";
    case kDiscardSlot3: return "DiscardSlot3";
    default: break;
  }
  if (action >= kCoupPlayer0 && action <= kCoupPlayer0 + 5) {
    return "Coup->" + std::to_string(action - kCoupPlayer0);
  }
  if (action >= kStealPlayer0 && action <= kStealPlayer0 + 5) {
    return "Steal->" + std::to_string(action - kStealPlayer0);
  }
  if (action >= kAssassinatePlayer0 && action <= kAssassinatePlayer0 + 5) {
    return "Assassinate->" + std::to_string(action - kAssassinatePlayer0);
  }
  return "Unknown(" + std::to_string(action) + ")";
}

// ===========================================================================
// CoupState -- ToString
// ===========================================================================

std::string CoupState::ToString() const {
  std::ostringstream ss;
  ss << "Phase=" << phase_ << " Turn=" << turn_player_
     << " Active=" << active_player_ << " Pending=" << pending_action_
     << " Winner=" << winner_ << "\n";
  for (int i = 0; i < NumPlayers(); i++) {
    ss << "P" << i << ": coins=" << players_[i].coins;
    for (int s = 0; s < kCardsPerPlayer; s++) {
      ss << " [" << CardName(players_[i].cards[s].type)
         << (players_[i].cards[s].alive ? "*" : "X") << "]";
    }
    ss << "\n";
  }
  ss << "Deck:";
  for (int i = 0; i < kNumCardTypes; i++) {
    ss << " " << CardName(i) << "=" << deck_[i];
  }
  ss << "\n";
  return ss.str();
}

// ===========================================================================
// CoupState -- InformationStateString
// ===========================================================================

std::string CoupState::InformationStateString(Player player) const {
  // Encodes: own cards, public information, full action history.
  // Two states are in the same information set iff this string matches.
  std::ostringstream ss;
  ss << "p" << player << ":";

  // Own cards (private).
  for (int s = 0; s < kCardsPerPlayer; s++) {
    ss << players_[player].cards[s].type
       << (players_[player].cards[s].alive ? "a" : "d");
  }
  ss << "c" << players_[player].coins;

  // Public info for all players: revealed cards, coins, alive status.
  ss << "|pub:";
  for (int i = 0; i < NumPlayers(); i++) {
    ss << "P" << i << ":";
    for (int s = 0; s < kCardsPerPlayer; s++) {
      if (!players_[i].cards[s].alive) {
        // Revealed card: type is public.
        ss << players_[i].cards[s].type << "d";
      } else {
        // Face-down: type hidden from others, but we include for self above.
        ss << "?a";
      }
    }
    ss << "c" << players_[i].coins << ";";
  }

  // Phase info.
  ss << "|ph:" << phase_ << ",t:" << turn_player_ << ",a:" << active_player_
     << ",pa:" << pending_action_;

  // Responded mask + block info.
  ss << ",rm:" << static_cast<int>(responded_mask_);
  if (blocker_ >= 0) ss << ",bl:" << blocker_ << ":" << block_card_;

  // Exchange cards (only visible to turn_player_).
  if (player == turn_player_ && phase_ == kExchangeDiscard) {
    ss << ",ex:" << exchange_cards_[0] << "," << exchange_cards_[1];
    if (first_discard_ >= 0) ss << ",fd:" << first_discard_;
  }

  // Full action history from State::history_.
  ss << "|hist:";
  for (const auto& h : history_) {
    ss << h.action << ",";
  }

  return ss.str();
}

// ===========================================================================
// CoupState -- InformationStateTensor
// ===========================================================================

void CoupState::InformationStateTensor(Player player,
                                       absl::Span<float> values) const {
  FillObservationTensor(player, values);
}

void CoupState::FillObservationTensor(Player player,
                                      absl::Span<float> values) const {
  assert(static_cast<int>(values.size()) >= kObservationTensorSize);
  std::fill(values.begin(), values.begin() + kObservationTensorSize, 0.0f);
  int offset = 0;

  // 0-71: all players' cards — absolute encoding (6 players x 12 floats)
  // Per card: type one-hot (5) + alive flag (1)
  // Type visible if: (a) observer's own card, or (b) card is dead/revealed.
  for (int p = 0; p < kMaxPlayers; p++) {
    int base = p * 12;
    if (p < NumPlayers()) {
      bool is_self = (p == player);
      // Card 0.
      bool c0_alive = players_[p].cards[0].alive;
      if (is_self || !c0_alive) {
        values[base + players_[p].cards[0].type] = 1.0f;
      }
      values[base + 5] = c0_alive ? 1.0f : 0.0f;
      // Card 1.
      bool c1_alive = players_[p].cards[1].alive;
      if (is_self || !c1_alive) {
        values[base + 6 + players_[p].cards[1].type] = 1.0f;
      }
      values[base + 11] = c1_alive ? 1.0f : 0.0f;
    }
  }
  offset = 72;

  // 72-77: all_coins normalized /12 (6)
  for (int i = 0; i < kMaxPlayers; i++) {
    if (i < NumPlayers()) {
      values[offset + i] =
          static_cast<float>(players_[i].coins) / kMaxCoins;
    }
  }
  offset += 6;

  // 78-83: alive_mask (6)
  for (int i = 0; i < kMaxPlayers; i++) {
    if (i < NumPlayers() && PlayerIsAlive(i)) {
      values[offset + i] = 1.0f;
    }
  }
  offset += 6;

  // 84-90: phase one-hot (7 phases: MAIN_ACTION through EXCHANGE_DISCARD)
  // Map: MAIN_ACTION=0, CHALLENGE_ACTION=1, BLOCK=2, CHALLENGE_BLOCK=3,
  //       LOSE_CARD=4, EXCHANGE_DISCARD=5, RESOLVE=6
  // Chance phases are not encoded here (they map to nothing).
  if (phase_ >= kMainAction && phase_ <= kResolve) {
    values[offset + (phase_ - kMainAction)] = 1.0f;
  }
  offset += 7;

  // 91-96: active_player one-hot (6)
  if (active_player_ >= 0 && active_player_ < kMaxPlayers) {
    values[offset + active_player_] = 1.0f;
  }
  offset += 6;

  // 97-102: turn_player one-hot (6)
  values[offset + turn_player_] = 1.0f;
  offset += 6;

  // 103-134: pending_action one-hot (32)
  if (pending_action_ >= 0 && pending_action_ < kNumActions) {
    values[offset + pending_action_] = 1.0f;
  }
  offset += 32;

  // 135-140: responded_mask (6)
  for (int i = 0; i < kMaxPlayers; i++) {
    values[offset + i] = (responded_mask_ & (1 << i)) ? 1.0f : 0.0f;
  }
  offset += 6;

  // 141-150: exchange_cards one-hot x 2 (10 = 2 x 5)
  // Only visible to turn_player during exchange.
  if (player == turn_player_ &&
      (phase_ == kExchangeDiscard || phase_ == kChanceExchange)) {
    if (exchange_cards_[0] >= 0 && exchange_cards_[0] < kNumCardTypes) {
      values[offset + exchange_cards_[0]] = 1.0f;
    }
    if (exchange_cards_[1] >= 0 && exchange_cards_[1] < kNumCardTypes) {
      values[offset + 5 + exchange_cards_[1]] = 1.0f;
    }
  }
  offset += 10;

  // 151-406: history x 4 floats each (256 = 64 x 4)
  // Use State::history_ to fill. Each action encoded as:
  //   acting_player/6, action/32, phase/10, result_flags
  // We use our history_buffer_ which tracks acting_player, action, phase,
  // result.
  int hist_start = std::max(0, static_cast<int>(history_buffer_.size()) -
                                   kHistoryLength);
  for (int i = hist_start; i < static_cast<int>(history_buffer_.size()); i++) {
    int idx = i - hist_start;
    if (idx >= kHistoryLength) break;
    int base = offset + idx * kHistoryEntrySize;
    values[base + 0] =
        static_cast<float>(history_buffer_[i].acting_player) / 6.0f;
    values[base + 1] =
        static_cast<float>(history_buffer_[i].action) / 32.0f;
    values[base + 2] =
        static_cast<float>(history_buffer_[i].phase) / 10.0f;
    values[base + 3] = static_cast<float>(history_buffer_[i].result);
  }
  // offset += 256;  // total = 407
}

// ===========================================================================
// CoupState -- ObservationString / ObservationTensor
// ===========================================================================

std::string CoupState::ObservationString(Player player) const {
  // For now, reuse InformationStateString. A proper observation string
  // would exclude full history for a more compact representation.
  return InformationStateString(player);
}

void CoupState::ObservationTensor(Player player,
                                  absl::Span<float> values) const {
  FillObservationTensor(player, values);
}

}  // namespace coup
}  // namespace open_spiel
