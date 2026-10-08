// coup_game.cc — OpenSpiel adapter over the C engine. See coup_game.h.

#include "coup_game.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace coup {
namespace {

const GameType kGameType{
    /*short_name=*/"coup",
    /*long_name=*/"Coup",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kExplicitStochastic,
    GameType::Information::kImperfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/kMaxPlayers,
    /*min_num_players=*/kMinPlayers,
    /*provides_information_state_string=*/true,
    // Tabular CFR/MCCFR key on the (perfect-recall) InformationStateString.
    // The tensor is a fixed-size summary of it for neural methods (Deep CFR,
    // NFSP, R-NaD, ...); see CoupState::InformationStateTensor.
    /*provides_information_state_tensor=*/true,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    {{"players", GameParameter(kDefaultPlayers)},
     {"refund_on_challenge", GameParameter(kDefaultRefundOnChallenge)}},
};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::make_shared<CoupGame>(params);
}

REGISTER_SPIEL_GAME(kGameType, Factory);

// Who is challenged, for which role, and whether they hold it. Mirrors the C
// engine's step_deterministic CHALLENGE handling.
bool ChallengedClaimIsTrue(const ::Game& g, int* claimant_out, int* role_out) {
  int claimant, role;
  if (get_phase(&g) == PHASE_CHALLENGE_ACTION) {
    claimant = get_turn_player(&g);
    role = coup_obs_action_role(get_pending_action(&g));
  } else {
    claimant = get_blocker(&g);
    role = get_block_card(&g);
  }
  *claimant_out = claimant;
  *role_out = role;
  return (player_card0_alive(&g, claimant) &&
          player_card0_type(&g, claimant) == role) ||
         (player_card1_alive(&g, claimant) &&
          player_card1_type(&g, claimant) == role);
}

// Thermometer encoding of min(count, width) into `width` bits.
void Thermometer(int count, int width, float* out) {
  for (int i = 0; i < width; ++i) out[i] = count > i ? 1.0f : 0.0f;
}

// Index of a role-claiming main action in the info-state counters, or -1.
int ClaimActionIndex(int a) {
  if (a == ACT_TAX) return 0;
  if (a == ACT_EXCHANGE) return 1;
  if (a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6) return 2;
  if (a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6) return 3;
  return -1;
}

int BlockIndex(int a) {
  switch (a) {
    case ACT_BLOCK_DUKE: return 0;
    case ACT_BLOCK_CONTESSA: return 1;
    case ACT_BLOCK_CAPTAIN: return 2;
    case ACT_BLOCK_AMBASSADOR: return 3;
    default: return -1;
  }
}

// Number of cards dealt so far (only meaningful during PHASE_DEAL).
int CardsDealt(const ::Game& g) {
  return get_phase(&g) == PHASE_DEAL ? get_pending_action(&g)
                                     : 2 * get_num_players(&g);
}

}  // namespace

std::string CardName(int card) {
  switch (card) {
    case DUKE: return "Duke";
    case ASSASSIN: return "Assassin";
    case CAPTAIN: return "Captain";
    case AMBASSADOR: return "Ambassador";
    case CONTESSA: return "Contessa";
    default: return absl::StrCat("Card", card);
  }
}

std::string ActionName(int a) {
  if (a == ACT_INCOME) return "Income";
  if (a == ACT_FOREIGN_AID) return "ForeignAid";
  if (a == ACT_TAX) return "Tax";
  if (a == ACT_EXCHANGE) return "Exchange";
  if (a >= ACT_COUP_P0 && a < ACT_COUP_P0 + 6)
    return absl::StrCat("Coup->p", a - ACT_COUP_P0);
  if (a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6)
    return absl::StrCat("Steal->p", a - ACT_STEAL_P0);
  if (a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6)
    return absl::StrCat("Assassinate->p", a - ACT_ASSASSINATE_P0);
  if (a == ACT_CHALLENGE) return "Challenge";
  if (a == ACT_PASS) return "Pass";
  if (a == ACT_BLOCK_CONTESSA) return "Block(Contessa)";
  if (a == ACT_BLOCK_CAPTAIN) return "Block(Captain)";
  if (a == ACT_BLOCK_AMBASSADOR) return "Block(Ambassador)";
  if (a == ACT_BLOCK_DUKE) return "Block(Duke)";
  if (a >= ACT_DISCARD_SLOT0 && a <= ACT_DISCARD_SLOT3)
    return absl::StrCat("Discard", a - ACT_DISCARD_SLOT0);
  return absl::StrCat("Action", a);
}

std::string PhaseName(int phase) {
  switch (phase) {
    case PHASE_DEAL: return "Deal";
    case PHASE_CHANCE_REDRAW: return "ChanceRedraw";
    case PHASE_CHANCE_EXCHANGE: return "ChanceExchange";
    case PHASE_MAIN_ACTION: return "MainAction";
    case PHASE_CHALLENGE_ACTION: return "ChallengeAction";
    case PHASE_BLOCK: return "Block";
    case PHASE_CHALLENGE_BLOCK: return "ChallengeBlock";
    case PHASE_LOSE_CARD: return "LoseCard";
    case PHASE_EXCHANGE_DISCARD: return "ExchangeDiscard";
    case PHASE_RESOLVE: return "Resolve";
    default: return absl::StrCat("Phase", phase);
  }
}

// ===========================================================================
// CoupGame
// ===========================================================================

CoupGame::CoupGame(const GameParameters& params)
    : Game(kGameType, params),
      num_players_(ParameterValue<int>("players")),
      refund_on_challenge_(ParameterValue<bool>("refund_on_challenge")) {
  SPIEL_CHECK_GE(num_players_, kMinPlayers);
  SPIEL_CHECK_LE(num_players_, kMaxPlayers);
}

std::unique_ptr<State> CoupGame::NewInitialState() const {
  return std::make_unique<CoupState>(shared_from_this(), num_players_,
                                     refund_on_challenge_);
}

// ===========================================================================
// CoupState
// ===========================================================================

CoupState::CoupState(std::shared_ptr<const Game> game, int num_players,
                     bool refund_on_challenge)
    : State(std::move(game)) {
  // Both seeds 0 => game_init does NOT auto-deal: the state is left in
  // PHASE_DEAL so the 2n deal draws are explicit chance nodes. The RNG inside
  // Game is seeded but never consumed by this adapter.
  game_init(&g_, num_players, 0, 0);
  game_set_refund_on_challenge(&g_, refund_on_challenge ? 1 : 0);
  coup_obs_tracker_reset(&tracker_);
  events_.reserve(64);
}

Player CoupState::CurrentPlayer() const {
  if (IsTerminal()) return kTerminalPlayerId;
  if (is_chance_node(&g_)) return kChancePlayerId;
  return get_active_player(&g_);
}

bool CoupState::IsTerminal() const {
  // The deal is never terminal (guarded here too, since mid-deal most seats
  // have no living cards yet).
  return get_phase(&g_) != PHASE_DEAL && is_done(&g_);
}

std::vector<Action> CoupState::LegalActions() const {
  if (IsTerminal()) return {};
  std::vector<Action> actions;
  if (is_chance_node(&g_)) {
    ChanceOutcome out[MAX_CHANCE_OUTCOMES];
    int n = chance_outcomes(&g_, out);
    actions.reserve(n);
    for (int i = 0; i < n; ++i) actions.push_back(out[i].outcome);
    return actions;
  }
  uint32_t mask = get_valid_actions(&g_);
  for (int a = 0; a < kNumActions; ++a) {
    if (mask & (1u << a)) actions.push_back(a);
  }
  return actions;
}

std::vector<std::pair<Action, double>> CoupState::ChanceOutcomes() const {
  SPIEL_CHECK_TRUE(IsChanceNode());
  ChanceOutcome out[MAX_CHANCE_OUTCOMES];
  int n = chance_outcomes(&g_, out);
  std::vector<std::pair<Action, double>> outcomes;
  outcomes.reserve(n);
  for (int i = 0; i < n; ++i) outcomes.emplace_back(out[i].outcome, out[i].prob);
  return outcomes;
}

void CoupState::DoApplyAction(Action action) {
  LoggedEvent e;
  e.phase = static_cast<uint8_t>(get_phase(&g_));
  e.action = static_cast<uint8_t>(action);
  e.info = 0;
  e.claimant = 0;
  e.role = 0;

  if (is_chance_node(&g_)) {
    SPIEL_CHECK_GE(action, 0);
    SPIEL_CHECK_LT(action, kNumCardTypes);
    SPIEL_CHECK_GT(deck_count(&g_, action), 0);
    switch (e.phase) {
      case PHASE_DEAL:
        e.actor = static_cast<uint8_t>(get_pending_action(&g_) / 2);
        break;
      case PHASE_CHANCE_REDRAW:
        e.actor = static_cast<uint8_t>(get_active_player(&g_));
        e.info = static_cast<uint8_t>(get_exchange_card0(&g_));  // slot
        break;
      default:  // PHASE_CHANCE_EXCHANGE
        e.actor = static_cast<uint8_t>(get_turn_player(&g_));
        break;
    }
    SPIEL_CHECK_EQ(apply_chance(&g_, static_cast<int>(action)), 0);
  } else {
    SPIEL_CHECK_FALSE(IsTerminal());
    SPIEL_CHECK_TRUE(action >= 0 && action < kNumActions &&
                     (get_valid_actions(&g_) & (1u << action)));
    int actor = get_active_player(&g_);
    e.actor = static_cast<uint8_t>(actor);
    if (e.phase == PHASE_LOSE_CARD) {
      e.info = static_cast<uint8_t>(action == ACT_DISCARD_SLOT0
                                        ? player_card0_type(&g_, actor)
                                        : player_card1_type(&g_, actor));
    } else if (action == ACT_CHALLENGE) {
      int claimant, role;
      e.info = ChallengedClaimIsTrue(g_, &claimant, &role) ? 1 : 0;
      e.claimant = static_cast<uint8_t>(claimant);
      e.role = static_cast<uint8_t>(role);
    } else if (e.phase == PHASE_EXCHANGE_DISCARD) {
      const int slot = static_cast<int>(action) - ACT_DISCARD_SLOT0;
      const int cards[4] = {player_card0_type(&g_, actor),
                            player_card1_type(&g_, actor),
                            get_exchange_card0(&g_), get_exchange_card1(&g_)};
      e.info = static_cast<uint8_t>(cards[slot]);
    }
    coup_obs_tracker_record(&tracker_, &g_, actor, static_cast<int>(action));
    SPIEL_CHECK_EQ(step_deterministic(&g_, static_cast<int>(action)), 0);
  }
  events_.push_back(e);
}

void CoupState::UndoAction(Player /*player*/, Action action) {
  // The engine has no inverse step; replay the prefix. O(history) but only
  // used by tests/tools, never by the hot path.
  SPIEL_CHECK_FALSE(history_.empty());
  SPIEL_CHECK_EQ(history_.back().action, action);
  const auto* game = static_cast<const CoupGame*>(game_.get());
  CoupState fresh(game_, num_players_, game->refund_on_challenge());
  for (auto it = history_.begin(); it + 1 != history_.end(); ++it) {
    fresh.ApplyAction(it->action);
  }
  g_ = fresh.g_;
  tracker_ = fresh.tracker_;
  events_ = std::move(fresh.events_);
  history_ = std::move(fresh.history_);
  move_number_ = fresh.move_number_;
}

std::string CoupState::ActionToString(Player player, Action action) const {
  if (player == kChancePlayerId) {
    switch (get_phase(&g_)) {
      case PHASE_DEAL:
        return absl::StrCat("Deal p", get_pending_action(&g_) / 2, " ",
                            CardName(action));
      case PHASE_CHANCE_REDRAW:
        return absl::StrCat("Redraw p", get_active_player(&g_), " ",
                            CardName(action));
      case PHASE_CHANCE_EXCHANGE:
        return absl::StrCat("ExchangeDraw p", get_turn_player(&g_), " ",
                            CardName(action));
      default:
        return absl::StrCat("Chance ", CardName(action));
    }
  }
  return absl::StrCat("p", player, " ", ActionName(action));
}

std::string CoupState::InformationStateString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  // Perfect recall, no leaks. Tokens, in order:
  //   deal<q>:<card|?>          card dealt to seat q (own cards only)
  //   redraw<q>.s<k>:<card|?>   replacement after a defended challenge; the
  //                             slot is public (the revealed card's slot)
  //   draw<q>:<card|?>          exchange draw (own draws only)
  //   <q>:<action>              public decision by seat q, with
  //     <q>:Challenge[won|lost] outcome is public (claimant reveals or loses)
  //     <q>:Lose<k>=<card>      the lost card is turned face up
  //     <q>:ExchDiscard?        another player's exchange pick (private)
  std::string s = absl::StrCat("p", player, " n", num_players_);
  for (const LoggedEvent& e : events_) {
    const bool own = e.actor == player;
    switch (e.phase) {
      case PHASE_DEAL:
        absl::StrAppend(&s, " deal", e.actor, ":",
                        own ? CardName(e.action) : "?");
        break;
      case PHASE_CHANCE_REDRAW:
        absl::StrAppend(&s, " redraw", e.actor, ".s", e.info, ":",
                        own ? CardName(e.action) : "?");
        break;
      case PHASE_CHANCE_EXCHANGE:
        absl::StrAppend(&s, " draw", e.actor, ":",
                        own ? CardName(e.action) : "?");
        break;
      case PHASE_EXCHANGE_DISCARD:
        if (own) {
          absl::StrAppend(&s, " ", e.actor, ":ExchDiscard",
                          e.action - ACT_DISCARD_SLOT0);
        } else {
          absl::StrAppend(&s, " ", e.actor, ":ExchDiscard?");
        }
        break;
      case PHASE_LOSE_CARD:
        absl::StrAppend(&s, " ", e.actor, ":Lose",
                        e.action - ACT_DISCARD_SLOT0, "=", CardName(e.info));
        break;
      default:
        if (e.action == ACT_CHALLENGE) {
          absl::StrAppend(&s, " ", e.actor, ":Challenge",
                          e.info ? "[lost]" : "[won]");
        } else {
          absl::StrAppend(&s, " ", e.actor, ":", ActionName(e.action));
        }
        break;
    }
  }
  return s;
}

std::string CoupState::ObservationString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  const int phase = get_phase(&g_);
  const int dealt = CardsDealt(g_);
  std::string s = absl::StrCat("p", player, " turn ", g_.turn_count, " ",
                               PhaseName(phase));
  if (phase != PHASE_DEAL) {
    absl::StrAppend(&s, " turn_player=p", get_turn_player(&g_));
    if (!IsTerminal() && !is_chance_node(&g_)) {
      absl::StrAppend(&s, " to_act=p", get_active_player(&g_));
    }
    if (phase != PHASE_MAIN_ACTION) {
      absl::StrAppend(&s, " pending=", ActionName(get_pending_action(&g_)));
    }
    if (phase == PHASE_CHALLENGE_BLOCK) {
      absl::StrAppend(&s, " block=p", get_blocker(&g_), "/",
                      CardName(get_block_card(&g_)));
    }
  }
  for (int q = 0; q < num_players_; ++q) {
    absl::StrAppend(&s, "\n p", q, " coins=", player_coins(&g_, q), " [");
    for (int k = 0; k < 2; ++k) {
      if (k) absl::StrAppend(&s, " ");
      if (2 * q + k >= dealt) {
        absl::StrAppend(&s, "-");
        continue;
      }
      int alive = k ? player_card1_alive(&g_, q) : player_card0_alive(&g_, q);
      int type = k ? player_card1_type(&g_, q) : player_card0_type(&g_, q);
      if (!alive) {
        absl::StrAppend(&s, CardName(type), "(dead)");
      } else if (q == player) {
        absl::StrAppend(&s, CardName(type));
      } else {
        absl::StrAppend(&s, "?");
      }
    }
    absl::StrAppend(&s, "]");
  }
  if (phase == PHASE_EXCHANGE_DISCARD && get_turn_player(&g_) == player) {
    absl::StrAppend(&s, "\n drawn=[", CardName(get_exchange_card0(&g_)), " ",
                    CardName(get_exchange_card1(&g_)), "]");
    if (get_first_discard(&g_) != FIRST_DISCARD_NONE) {
      absl::StrAppend(&s, " first_discard=", get_first_discard(&g_));
    }
  }
  return s;
}

void CoupState::ObservationTensor(Player player,
                                  absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  SPIEL_CHECK_EQ(static_cast<int>(values.size()), kObservationTensorSize);
  uint8_t buf[COUP_OBS_SIZE];
  coup_obs_write(&g_, &tracker_, player, buf);
  for (int i = 0; i < COUP_OBS_SIZE; ++i) values[i] = buf[i];
}

void CoupState::InformationStateTensor(Player player,
                                       absl::Span<float> values) const {
  // Layout (kInfoStateTensorSize floats, all 0/1):
  //   [0, COUP_OBS_SIZE)  the observation tensor (coup_obs.h)
  //   then kMaxPlayers relative seats x kInfoSeatSize, whole-game counters
  //   from public events (thermometer 3 unless noted):
  //     +0..11  role claims via main action: Tax, Exchange, Steal, Assassinate
  //     +12..23 blocks: Duke, Contessa, Captain, Ambassador
  //     +24..26 challenges made, +27..29 of which won, +30..32 of which lost
  //     +33..37 roles proven when challenged (1 bit per role)
  //     +38..42 roles caught bluffing (1 bit per role)
  //   then 5 roles x thermometer 3: cards the observer returned to the deck
  //   through their own exchanges (private).
  // Everything is derived from what InformationStateString(player) shows, so
  // the tensor is a function of the infostate. It is a summary, not an
  // injective encoding of the full history.
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  SPIEL_CHECK_EQ(static_cast<int>(values.size()), kInfoStateTensorSize);
  std::fill(values.begin(), values.end(), 0.0f);
  ObservationTensor(player, values.subspan(0, kObservationTensorSize));

  int claims[kMaxPlayers][4] = {};
  int blocks[kMaxPlayers][4] = {};
  int challenges[kMaxPlayers][3] = {};  // made, won, lost
  int proven[kMaxPlayers] = {};         // role bitmasks
  int caught[kMaxPlayers] = {};
  int returned[kNumCardTypes] = {};
  for (const LoggedEvent& e : events_) {
    if (e.phase == PHASE_DEAL || e.phase == PHASE_CHANCE_REDRAW ||
        e.phase == PHASE_CHANCE_EXCHANGE || e.phase == PHASE_LOSE_CARD) {
      continue;
    }
    if (e.phase == PHASE_EXCHANGE_DISCARD) {
      if (e.actor == player) ++returned[e.info];
      continue;
    }
    const int ci = ClaimActionIndex(e.action);
    const int bi = BlockIndex(e.action);
    if (e.phase == PHASE_MAIN_ACTION && ci >= 0) ++claims[e.actor][ci];
    if (bi >= 0) ++blocks[e.actor][bi];
    if (e.action == ACT_CHALLENGE) {
      ++challenges[e.actor][0];
      if (e.info) {
        ++challenges[e.actor][2];
        proven[e.claimant] |= 1 << e.role;
      } else {
        ++challenges[e.actor][1];
        caught[e.claimant] |= 1 << e.role;
      }
    }
  }

  float* out = values.data() + kObservationTensorSize;
  for (int r = 0; r < num_players_; ++r) {
    const int q = (player + r) % num_players_;
    float* s = out + r * kInfoSeatSize;
    for (int i = 0; i < 4; ++i) Thermometer(claims[q][i], 3, s + 3 * i);
    for (int i = 0; i < 4; ++i) Thermometer(blocks[q][i], 3, s + 12 + 3 * i);
    for (int i = 0; i < 3; ++i) Thermometer(challenges[q][i], 3, s + 24 + 3 * i);
    for (int c = 0; c < kNumCardTypes; ++c) {
      s[33 + c] = (proven[q] >> c) & 1;
      s[38 + c] = (caught[q] >> c) & 1;
    }
  }
  out += kMaxPlayers * kInfoSeatSize;
  for (int c = 0; c < kNumCardTypes; ++c) Thermometer(returned[c], 3, out + 3 * c);
}

// ---------------------------------------------------------------------------
// ResampleFromInfostate
//
// Samples a complete history that `player` cannot tell apart from the real
// one: same public actions, same own cards and draws, and every public
// revelation (lost cards, challenge outcomes, redraw slots) reproduced
// exactly when the history is replayed through the engine.
//
// Model: the 15 cards are tokens moving between deck, hands and exchange
// draws. Unobserved choices are resampled: opponents' exchange discard picks
// uniformly over legal picks (OpenSpiel's convention for unobserved actions),
// chance draws from the deck. Observations become constraints on a token's
// role: own draws and revealed cards fix it, a failed claim excludes the role
// for the claimant's live cards, a proven claim fixes the redraw slot (and
// excludes the role from slot 0 when the engine picked slot 1).
//
// Two passes per attempt:
//   1. Positions only. With the opponents' discard picks drawn up front, each
//      chance draw starts a "hand life" (the card's stay in a hand until it
//      returns to the deck) and every constraint lands on exactly one life.
//   2. Token flow. Each draw picks a deck token weighted by how likely it is
//      to satisfy its life's constraints (fixed tokens: 0/1; unfixed tokens:
//      share of still-unplaced copies of the allowed roles), then applies
//      them. Unfixed tokens finally get roles drawn uniformly from the
//      remaining multiset subject to exclusions (exact DP over role counts).
// Local repairs handle most dead ends: pass 1 re-picks one recent exchange
// of the seat whose observation failed; pass 2 swaps a needed token out of an
// unconstrained hand for one that was in the deck all along. An attempt that
// still dead-ends is discarded and retried (~1.2 attempts on average, a few
// hundred at worst over ~1.4M resamples of long 6-player games). The result
// is always a valid, consistent history; its distribution approximates (does
// not exactly equal) the chance-weighted posterior.
// ---------------------------------------------------------------------------
namespace {

struct Constraint {
  int8_t eq = -1;    // fixed role, or -1
  uint8_t neq = 0;   // bitmask of excluded roles

  bool Fix(int role) {
    if (eq >= 0) return eq == role;
    if (neq & (1 << role)) return false;
    eq = static_cast<int8_t>(role);
    return true;
  }
  bool Exclude(int role) {
    if (eq >= 0) return eq != role;
    neq |= static_cast<uint8_t>(1 << role);
    return true;
  }
  bool Allows(int role) const {
    return eq >= 0 ? eq == role : !(neq & (1 << role));
  }
};

bool IsDraw(int phase) {
  return phase == PHASE_DEAL || phase == PHASE_CHANCE_REDRAW ||
         phase == PHASE_CHANCE_EXCHANGE;
}

class ResampleAttempt {
 public:
  ResampleAttempt(int player, const std::vector<LoggedEvent>& events,
                  const std::function<double()>& rng)
      : player_(player), events_(events), rng_(rng),
        life_(events.size()), picks_(events.size(), -1) {}

  // Returns the history to replay, or false if this attempt is inconsistent.
  bool Run(const std::vector<State::PlayerAction>& history,
           std::vector<Action>* actions) {
    if (!PlanLives()) return false;
    int token_at[kMaxGameEvents];
    if (!FlowTokens(token_at)) return false;
    int roles[kDeckSize];
    if (!AssignRoles(roles)) return false;
    actions->resize(history.size());
    for (size_t i = 0; i < history.size(); ++i) {
      if (IsDraw(events_[i].phase)) {
        (*actions)[i] = roles[token_at[i]];
      } else if (picks_[i] >= 0) {
        (*actions)[i] = ACT_DISCARD_SLOT0 + picks_[i];
      } else {
        (*actions)[i] = history[i].action;
      }
    }
    return true;
  }

  static constexpr int kMaxGameEvents = 1 << 14;

 private:
  int Uniform(int n) {
    int k = static_cast<int>(rng_() * n);
    return k < n ? k : n - 1;
  }

  // Walks the public view of the history, moving "hand lives" (indexed by
  // the draw event that started them) around with the same slot rules as the
  // engine. `on_*` hooks let both passes share the walk. With `plan`, also
  // draws the opponents' discard picks and records constraints on lives.
  template <typename DrawFn, typename DiscardFn>
  bool Walk(DrawFn on_draw, DiscardFn on_return, bool plan) {
    fail_seat_ = -1;
    fail_event_ = events_.size();
    int hand[kMaxPlayers][2];
    bool alive[kMaxPlayers][2] = {};
    int xch[2] = {-1, -1};
    int xch_drawn = 0, dealt = 0, first = -1;
    int proof_claimant = -1, proof_role = -1;
    for (size_t i = 0; i < events_.size(); ++i) {
      const LoggedEvent& e = events_[i];
      const bool own = e.actor == player_;
      switch (e.phase) {
        case PHASE_DEAL: {
          const int k = dealt++ % 2;
          hand[e.actor][k] = static_cast<int>(i);
          alive[e.actor][k] = true;
          if (!on_draw(i, own)) return false;
          break;
        }
        case PHASE_CHANCE_REDRAW: {
          // begin_redraw: the proven card (slot e.info) returns to the deck;
          // the engine prefers slot 0 when it holds the role.
          const int k = e.info;
          SPIEL_CHECK_EQ(proof_claimant, e.actor);
          if (plan && (!life_[hand[e.actor][k]].Fix(proof_role) ||
                       (k == 1 && alive[e.actor][0] &&
                        !life_[hand[e.actor][0]].Exclude(proof_role)))) {
            return Fail(e.actor, i);
          }
          on_return(hand[e.actor][k], i);
          proof_claimant = -1;
          hand[e.actor][k] = static_cast<int>(i);
          if (!on_draw(i, own)) return false;
          break;
        }
        case PHASE_CHANCE_EXCHANGE:
          xch[xch_drawn++ % 2] = static_cast<int>(i);
          if (!on_draw(i, own)) return false;
          break;
        case PHASE_LOSE_CARD: {
          const int k = e.action - ACT_DISCARD_SLOT0;
          if (plan && !life_[hand[e.actor][k]].Fix(e.info)) {
            return Fail(e.actor, i);
          }
          alive[e.actor][k] = false;
          break;
        }
        case PHASE_EXCHANGE_DISCARD: {
          if (!own && plan && picks_[i] < 0) {
            // Same legality rule as get_valid_actions.
            unsigned avail = (alive[e.actor][0] ? 1u : 0u) |
                             (alive[e.actor][1] ? 2u : 0u) | 0xCu;
            avail &= first < 0 ? 0x7u : ~((2u << first) - 1);
            int opts[4], n = 0;
            for (int b = 0; b < 4; ++b) {
              if (avail & (1u << b)) opts[n++] = b;
            }
            SPIEL_CHECK_GT(n, 0);
            picks_[i] = opts[Uniform(n)];
          }
          const int slot =
              own ? e.action - ACT_DISCARD_SLOT0 : picks_[i];
          if (first < 0) {
            first = slot;
            break;
          }
          // Second pick: mirror step_deterministic's refill.
          int* h = hand[e.actor];
          const int cards[4] = {h[0], h[1], xch[0], xch[1]};
          unsigned keep = 0xFu & ~(1u << first) & ~(1u << slot);
          if (!alive[e.actor][0]) keep &= ~1u;
          if (!alive[e.actor][1]) keep &= ~2u;
          on_return(cards[first], i);
          on_return(cards[slot], i);
          if (alive[e.actor][0]) {
            h[0] = cards[__builtin_ctz(keep)];
            keep &= keep - 1;
          }
          if (alive[e.actor][1]) h[1] = cards[__builtin_ctz(keep)];
          first = -1;
          break;
        }
        default:
          if (e.action == ACT_CHALLENGE) {
            if (e.info) {
              proof_claimant = e.claimant;
              proof_role = e.role;
            } else if (plan) {
              for (int k = 0; k < 2; ++k) {
                if (alive[e.claimant][k] &&
                    !life_[hand[e.claimant][k]].Exclude(e.role)) {
                  return Fail(e.claimant, i);
                }
              }
            }
          }
          break;
      }
    }
    // A proven claim whose redraw has not happened yet (the challenger is
    // still choosing a card to lose): some live slot holds the role.
    if (plan && proof_claimant >= 0) {
      int opts[2], n = 0;
      for (int k = 0; k < 2; ++k) {
        if (alive[proof_claimant][k] &&
            life_[hand[proof_claimant][k]].Allows(proof_role)) {
          opts[n++] = k;
        }
      }
      if (n == 0) return Fail(proof_claimant, events_.size());
      life_[hand[proof_claimant][opts[Uniform(n)]]].Fix(proof_role);
    }
    return true;
  }

  bool Fail(int seat, size_t event) {
    fail_seat_ = seat;
    fail_event_ = event;
    return false;
  }

  // Pass 1: draw the opponents' discard picks and collect each hand life's
  // constraints. When the picks contradict a later observation of seat q,
  // only one of q's two latest exchanges before it is re-picked (local
  // repair) rather than starting over.
  bool PlanLives() {
    for (int tries = 0; tries < 64; ++tries) {
      std::fill(life_.begin(), life_.end(), Constraint());
      if (Walk([&](size_t i, bool own) {
                 return !own || life_[i].Fix(events_[i].action);
               },
               [](int, size_t) {}, /*plan=*/true)) {
        return true;
      }
      if (fail_seat_ < 0 || fail_seat_ == player_) return false;
      // Second-pick events of fail_seat_'s exchanges before the failure.
      size_t ex[2];
      int n = 0;
      for (size_t i = fail_event_; i-- > 0 && n < 2;) {
        const LoggedEvent& e = events_[i];
        if (e.phase == PHASE_EXCHANGE_DISCARD && e.actor == fail_seat_ &&
            i > 0 && events_[i - 1].phase == PHASE_EXCHANGE_DISCARD) {
          ex[n++] = i;
          --i;  // skip the first pick
        }
      }
      if (n == 0) return false;
      const size_t second = ex[Uniform(n)];
      picks_[second] = picks_[second - 1] = -1;
    }
    return false;
  }

  // Pass 2: assign a deck token to every hand life.
  bool FlowTokens(int* token_at) {
    if (events_.size() > static_cast<size_t>(kMaxGameEvents)) return false;
    std::vector<int> deck;
    for (int t = 0; t < kDeckSize; ++t) {
      deck.push_back(t);
      holder_[t] = -1;
      deck_since_[t] = 0;
    }
    return Walk(
        [&](size_t i, bool) {
          int t = DrawFor(life_[i], &deck);
          for (int r = 0; t < 0 && r < 4; ++r) {
            if (!SwapRepair(life_[i], token_at, &deck)) break;
            t = DrawFor(life_[i], &deck);
          }
          if (t < 0) return false;
          token_at[i] = t;
          holder_[t] = static_cast<int>(i);
          return true;
        },
        [&](int life, size_t i) {
          const int t = token_at[life];
          deck.push_back(t);
          holder_[t] = -1;
          deck_since_[t] = i;
        },
        /*plan=*/false);
  }

  // How likely deck token `tok` is to satisfy hand life `life`: 0/1 for a
  // fixed token, else the share of still-unplaced copies of roles both allow.
  double Weight(const Constraint& life, const Constraint& tok) const {
    if (tok.eq >= 0) return life.Allows(tok.eq) ? 1.0 : 0.0;
    if (unfixed_ == 0) return 0.0;
    int copies = 0;
    for (int c = 0; c < kNumCardTypes; ++c) {
      if (life.Allows(c) && tok.Allows(c)) copies += 3 - fixed_[c];
    }
    return double(copies) / unfixed_;
  }

  // No deck token can satisfy `life`. Find a token X that could, held by an
  // opponent's unconstrained hand life L, and a deck token Y that has been in
  // the deck since before L drew X and suits L: then L could equally have
  // drawn Y, which puts X back in the deck. Swaps one random such pair.
  bool SwapRepair(const Constraint& life, int* token_at,
                  std::vector<int>* deck) {
    std::vector<std::pair<int, int>> pairs;  // (X, index of Y in deck)
    for (int x = 0; x < kDeckSize; ++x) {
      const int l = holder_[x];
      if (l < 0 || life_[l].eq >= 0 || Weight(life, tokens_[x]) <= 0) continue;
      for (int j = 0; j < static_cast<int>(deck->size()); ++j) {
        const int y = (*deck)[j];
        if (deck_since_[y] <= static_cast<size_t>(l) &&
            Weight(life_[l], tokens_[y]) > 0) {
          pairs.emplace_back(x, j);
        }
      }
    }
    if (pairs.empty()) return false;
    const auto [x, j] = pairs[Uniform(static_cast<int>(pairs.size()))];
    const int l = holder_[x];
    const int y = (*deck)[j];
    for (int c = 0; c < kNumCardTypes; ++c) {
      if ((life_[l].neq >> c) & 1) tokens_[y].Exclude(c);
    }
    (*deck)[j] = x;
    deck_since_[x] = l;
    holder_[x] = -1;
    holder_[y] = l;
    token_at[l] = y;
    return true;
  }

  // Picks a deck token for a hand life and applies the life's constraints.
  int DrawFor(const Constraint& life, std::vector<int>* deck) {
    const int n = static_cast<int>(deck->size());
    double w[kDeckSize], total = 0;
    for (int i = 0; i < n; ++i) {
      w[i] = Weight(life, tokens_[(*deck)[i]]);
      total += w[i];
    }
    if (total <= 0) return -1;
    double u = rng_() * total;
    int idx = -1;
    for (int i = 0; i < n && idx < 0; ++i) {
      if (w[i] > 0 && (u -= w[i]) < 0) idx = i;
    }
    if (idx < 0) {  // rounding: take the last candidate
      for (int i = n - 1; i >= 0 && idx < 0; --i) {
        if (w[i] > 0) idx = i;
      }
    }
    const int t = (*deck)[idx];
    (*deck)[idx] = deck->back();
    deck->pop_back();
    Constraint& tok = tokens_[t];
    if (life.eq >= 0 && tok.eq < 0) {
      if (fixed_[life.eq] == 3 || !tok.Fix(life.eq)) return -1;
      ++fixed_[life.eq];
      --unfixed_;
    }
    for (int c = 0; c < kNumCardTypes; ++c) {
      if ((life.neq >> c) & 1) tok.Exclude(c);  // compatible by weight > 0
    }
    return t;
  }

  // Unfixed tokens get a uniformly random arrangement of the remaining
  // roles that respects their exclusions.
  bool AssignRoles(int roles_out[kDeckSize]) {
    int remaining[kNumCardTypes] = {3, 3, 3, 3, 3};
    std::vector<int> unfixed;
    for (int t = 0; t < kDeckSize; ++t) {
      if (tokens_[t].eq >= 0) {
        if (--remaining[tokens_[t].eq] < 0) return false;
        roles_out[t] = tokens_[t].eq;
      } else {
        unfixed.push_back(t);
      }
    }
    // ways[i][st]: completions of unfixed[i..] given remaining role counts
    // `st` (base 4, one digit per role).
    constexpr int kStates = 1 << (2 * kNumCardTypes);
    const int m = static_cast<int>(unfixed.size());
    std::vector<double> ways((m + 1) * kStates, 0.0);
    ways[m * kStates] = 1.0;
    for (int i = m - 1; i >= 0; --i) {
      const uint8_t neq = tokens_[unfixed[i]].neq;
      for (int st = 0; st < kStates; ++st) {
        double w = 0;
        for (int c = 0; c < kNumCardTypes; ++c) {
          const int cnt = (st >> (2 * c)) & 3;
          if (cnt == 0 || (neq & (1 << c))) continue;
          w += cnt * ways[(i + 1) * kStates + st - (1 << (2 * c))];
        }
        ways[i * kStates + st] = w;
      }
    }
    int st = 0;
    for (int c = 0; c < kNumCardTypes; ++c) st |= remaining[c] << (2 * c);
    if (ways[st] <= 0) return false;
    for (int i = 0; i < m; ++i) {
      const uint8_t neq = tokens_[unfixed[i]].neq;
      double u = rng_() * ways[i * kStates + st];
      int pick = -1;
      for (int c = 0; c < kNumCardTypes; ++c) {
        const int cnt = (st >> (2 * c)) & 3;
        if (cnt == 0 || (neq & (1 << c))) continue;
        pick = c;
        u -= cnt * ways[(i + 1) * kStates + st - (1 << (2 * c))];
        if (u < 0) break;
      }
      SPIEL_CHECK_GE(pick, 0);
      roles_out[unfixed[i]] = pick;
      st -= 1 << (2 * pick);
    }
    return true;
  }

  const int player_;
  const std::vector<LoggedEvent>& events_;
  const std::function<double()>& rng_;
  std::vector<Constraint> life_;  // per draw event: its hand life
  std::vector<int> picks_;        // resampled opponent discard slots
  int fail_seat_ = -1;            // seat whose observation failed (pass 1)
  size_t fail_event_ = 0;
  Constraint tokens_[kDeckSize];
  int fixed_[kNumCardTypes] = {};  // tokens fixed to each role
  int holder_[kDeckSize];          // pass 2: hand life holding it, or -1
  size_t deck_since_[kDeckSize];   // pass 2: event it last entered the deck
  int unfixed_ = kDeckSize;
};

}  // namespace

std::unique_ptr<State> CoupState::ResampleFromInfostate(
    int player_id, std::function<double()> rng) const {
  SPIEL_CHECK_GE(player_id, 0);
  SPIEL_CHECK_LT(player_id, num_players_);
  constexpr int kMaxAttempts = 100000;
  std::vector<Action> actions;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    ResampleAttempt run(player_id, events_, rng);
    if (!run.Run(history_, &actions)) continue;
    const auto* game = static_cast<const CoupGame*>(game_.get());
    auto state = std::make_unique<CoupState>(game_, num_players_,
                                             game->refund_on_challenge());
    for (Action a : actions) state->ApplyAction(a);
    SPIEL_CHECK_EQ(state->InformationStateString(player_id),
                   InformationStateString(player_id));
    return state;
  }
  SpielFatalError(absl::StrCat("ResampleFromInfostate: no consistent history "
                               "found in ", kMaxAttempts, " attempts"));
}


std::string CoupState::ToString() const {
  const int phase = get_phase(&g_);
  const int dealt = CardsDealt(g_);
  std::string s = absl::StrCat("Turn ", g_.turn_count, " phase=",
                               PhaseName(phase));
  if (phase != PHASE_DEAL) {
    absl::StrAppend(&s, " turn_player=p", get_turn_player(&g_),
                    " active=p", get_active_player(&g_));
    if (phase != PHASE_MAIN_ACTION) {
      absl::StrAppend(&s, " pending=", ActionName(get_pending_action(&g_)));
    }
    if (phase == PHASE_CHALLENGE_BLOCK) {
      absl::StrAppend(&s, " block=p", get_blocker(&g_), "/",
                      CardName(get_block_card(&g_)));
    }
    if (phase == PHASE_EXCHANGE_DISCARD) {
      absl::StrAppend(&s, " drawn=[", CardName(get_exchange_card0(&g_)), " ",
                      CardName(get_exchange_card1(&g_)), "] first_discard=",
                      get_first_discard(&g_));
    }
  } else {
    absl::StrAppend(&s, " dealt=", dealt);
  }
  if (IsTerminal()) absl::StrAppend(&s, " TERMINAL winner=p", get_winner(&g_));
  for (int q = 0; q < num_players_; ++q) {
    absl::StrAppend(&s, "\n p", q, ": coins=", player_coins(&g_, q), " [");
    for (int k = 0; k < 2; ++k) {
      if (k) absl::StrAppend(&s, " ");
      if (2 * q + k >= dealt) {
        absl::StrAppend(&s, "-");
        continue;
      }
      int alive = k ? player_card1_alive(&g_, q) : player_card0_alive(&g_, q);
      int type = k ? player_card1_type(&g_, q) : player_card0_type(&g_, q);
      absl::StrAppend(&s, CardName(type), alive ? "" : "(dead)");
    }
    absl::StrAppend(&s, "]");
  }
  absl::StrAppend(&s, "\n deck:");
  for (int c = 0; c < kNumCardTypes; ++c) {
    absl::StrAppend(&s, " ", CardName(c), "=", deck_count(&g_, c));
  }
  return s;
}

std::vector<double> CoupState::Returns() const {
  if (!IsTerminal()) return std::vector<double>(num_players_, 0.0);
  const int winner = get_winner(&g_);
  std::vector<double> returns(num_players_, -1.0 / (num_players_ - 1));
  SPIEL_CHECK_GE(winner, 0);
  returns[winner] = 1.0;
  return returns;
}

std::unique_ptr<State> CoupState::Clone() const {
  return std::make_unique<CoupState>(*this);
}

}  // namespace coup
}  // namespace open_spiel
