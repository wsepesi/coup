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
  // coup_event_make checks legality (chance: card in the deck; decision: in
  // the valid-action mask, game not over) and records the public/private
  // facts of the action against the state before it.
  SPIEL_CHECK_TRUE(action >= 0 && action < kNumActions);
  SPIEL_CHECK_EQ(coup_event_make(&g_, static_cast<int>(action), &e), 0);
  if (is_chance_node(&g_)) {
    SPIEL_CHECK_EQ(apply_chance(&g_, static_cast<int>(action)), 0);
  } else {
    coup_obs_tracker_record(&tracker_, &g_, e.actor, static_cast<int>(action));
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
// ResampleFromInfostate: a thin wrapper over the C search API's
// coup_resample (c_engine/coup_search.c; the algorithm and its guarantees are
// documented there). It samples a replayable action list that `player`
// cannot tell apart from the real history; we replay it through a fresh
// CoupState and check the infostate matches.
// ---------------------------------------------------------------------------
std::unique_ptr<State> CoupState::ResampleFromInfostate(
    int player_id, std::function<double()> rng) const {
  SPIEL_CHECK_GE(player_id, 0);
  SPIEL_CHECK_LT(player_id, num_players_);
  constexpr int kMaxAttempts = 100000;
  std::vector<uint8_t> actions(events_.size());
  auto uniform = [](void* ctx) {
    return (*static_cast<std::function<double()>*>(ctx))();
  };
  const int rc = coup_resample(events_.data(), static_cast<int>(events_.size()),
                               num_players_, player_id, uniform, &rng,
                               actions.data(), kMaxAttempts);
  if (rc < 0) {
    SpielFatalError(absl::StrCat("ResampleFromInfostate: coup_resample "
                                 "failed (", rc, ") after up to ",
                                 kMaxAttempts, " attempts"));
  }
  const auto* game = static_cast<const CoupGame*>(game_.get());
  auto state = std::make_unique<CoupState>(game_, num_players_,
                                           game->refund_on_challenge());
  for (uint8_t a : actions) state->ApplyAction(a);
  SPIEL_CHECK_EQ(state->InformationStateString(player_id),
                 InformationStateString(player_id));
  return state;
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
