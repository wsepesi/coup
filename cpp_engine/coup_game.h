// coup_game.h — OpenSpiel adapter over the C engine (c_engine/coup_core.c).
//
// There is exactly one implementation of the Coup rules in this repo: the C
// engine. This file only adapts it to OpenSpiel's State/Game interface, so
// OpenSpiel (CFR/MCCFR, exploitability, ...) and PufferLib/web/TUI all play
// the same game by construction.
//
// Mapping:
//   - State     = C `Game` (POD) + per-player event log (for infostates) +
//                 CoupObsTracker (for observation tensors). Clone = copy.
//   - Tensors   = ObservationTensor: coup_obs.h (current state + last 8
//                 events), identical to the PufferLib observation.
//                 InformationStateTensor: the same plus whole-game per-seat
//                 claim/block/challenge counters and the observer's own
//                 returned cards. A function of InformationStateString, but
//                 a fixed-size summary (not injective over histories).
//   - Resample  = ResampleFromInfostate samples a full history consistent with
//                 one player's infostate (see coup_game.cc), for IS-MCTS etc.
//   - Chance    = C chance nodes (deal, challenge redraw, exchange draws).
//                 Outcome index = card type (0..4). The RNG embedded in `Game`
//                 is never used: game_init(g, n, 0, 0) leaves the deal as
//                 explicit chance nodes and we never call step_with_rng.
//   - Decisions = step_deterministic with ABSOLUTE engine actions (0..31).
//                 (coup_obs.h's relative action indices only appear inside the
//                 observation tensor.)
//   - Returns   = winner +1, every other player -1/(n-1): zero-sum for any n.
//                 At MAX_TURNS the C engine's get_winner() tiebreak decides.
#ifndef COUP_CPP_ENGINE_COUP_GAME_H_
#define COUP_CPP_ENGINE_COUP_GAME_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/spiel.h"

// Both headers carry their own extern "C" guards.
#include "coup_core.h"
#include "coup_obs.h"

namespace open_spiel {
namespace coup {

inline constexpr int kMinPlayers = 2;
inline constexpr int kMaxPlayers = MAX_PLAYERS;
inline constexpr int kDefaultPlayers = 2;
inline constexpr bool kDefaultRefundOnChallenge = true;
inline constexpr int kNumActions = COUP_NUM_ACTIONS;  // 32
inline constexpr int kNumCardTypes = 5;
inline constexpr int kDeckSize = 15;  // 3 copies x 5 roles
inline constexpr int kObservationTensorSize = COUP_OBS_SIZE;
// InformationStateTensor = observation + per relative seat counters + own
// returned cards. See CoupState::InformationStateTensor.
inline constexpr int kInfoSeatSize = 43;
inline constexpr int kInfoStateTensorSize =
    kObservationTensorSize + kMaxPlayers * kInfoSeatSize + 5 * 3;

// Upper bound on player decisions in one turn (one main action):
//   1 main action
//   + 3 response windows (challenge-action, block, challenge-block), each at
//     most n-1 decisions (every other player passes, or someone acts)
//   + 3 LOSE_CARD decisions (lost challenge on the action, lost challenge on
//     the block, assassination/coup resolution)
//   + 2 EXCHANGE_DISCARD picks
// = 3n + 3. Loose (no single turn hits every term) but sound. A game has at
// most MAX_TURNS turns: every turn ends in advance_turn() (turn_count++) or
// game over, and is_done() fires at turn_count >= MAX_TURNS.
inline int MaxDecisionsPerTurn(int num_players) { return 3 * num_players + 3; }
// Chance nodes: 2n deal + at most 3 per turn (one redraw after a defended
// action challenge + two exchange draws; a defended block challenge redraw
// cannot co-occur with an exchange).
inline int MaxChancePerTurn() { return 3; }

// One entry per applied action (chance or decision). Holds the extra facts
// needed to render each player's information state without replaying.
struct LoggedEvent {
  uint8_t phase;   // engine phase BEFORE the action
  uint8_t actor;   // decision: acting seat; chance: seat receiving the card
  uint8_t action;  // decision: absolute action; chance: card type drawn
  uint8_t info;    // LOSE_CARD: revealed card type;
                   // CHALLENGE: 1 if the claim was true (challenger loses);
                   // CHANCE_REDRAW: hand slot being replaced;
                   // EXCHANGE_DISCARD: type of the discarded card (private)
  uint8_t claimant;  // CHALLENGE: seat whose claim is challenged
  uint8_t role;      // CHALLENGE: the challenged role
};

class CoupGame;

class CoupState : public State {
 public:
  explicit CoupState(std::shared_ptr<const Game> game, int num_players,
                     bool refund_on_challenge);
  CoupState(const CoupState&) = default;

  Player CurrentPlayer() const override;
  std::vector<Action> LegalActions() const override;
  std::string ActionToString(Player player, Action action) const override;
  std::string ToString() const override;
  bool IsTerminal() const override;
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  void InformationStateTensor(Player player,
                              absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  std::unique_ptr<State> ResampleFromInfostate(
      int player_id, std::function<double()> rng) const override;
  std::vector<std::pair<Action, double>> ChanceOutcomes() const override;
  void UndoAction(Player player, Action action) override;

  // Read-only access to the underlying C engine state (tests, tools).
  const ::Game& engine() const { return g_; }
  const CoupObsTracker& tracker() const { return tracker_; }

 protected:
  void DoApplyAction(Action action) override;

 private:
  ::Game g_;
  CoupObsTracker tracker_;
  std::vector<LoggedEvent> events_;
};

class CoupGame : public Game {
 public:
  explicit CoupGame(const GameParameters& params);

  int NumDistinctActions() const override { return kNumActions; }
  std::unique_ptr<State> NewInitialState() const override;
  int MaxChanceOutcomes() const override { return MAX_CHANCE_OUTCOMES; }
  int NumPlayers() const override { return num_players_; }
  double MinUtility() const override { return -1.0 / (num_players_ - 1); }
  double MaxUtility() const override { return 1.0; }
  absl::optional<double> UtilitySum() const override { return 0.0; }
  std::vector<int> ObservationTensorShape() const override {
    return {kObservationTensorSize};
  }
  std::vector<int> InformationStateTensorShape() const override {
    return {kInfoStateTensorSize};
  }
  int MaxGameLength() const override {
    return MAX_TURNS * MaxDecisionsPerTurn(num_players_);
  }
  int MaxChanceNodesInHistory() const override {
    return 2 * num_players_ + MAX_TURNS * MaxChancePerTurn();
  }

  bool refund_on_challenge() const { return refund_on_challenge_; }

 private:
  int num_players_;
  bool refund_on_challenge_;
};

// Human-readable names.
std::string CardName(int card);
std::string ActionName(int action);  // absolute engine action, no actor
std::string PhaseName(int phase);

}  // namespace coup
}  // namespace open_spiel

#endif  // COUP_CPP_ENGINE_COUP_GAME_H_
