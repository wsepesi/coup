// coup_game.h -- OpenSpiel integration for Coup
// Pure C++ implementation, no dependency on the C engine.

#ifndef OPEN_SPIEL_GAMES_COUP_GAME_H_
#define OPEN_SPIEL_GAMES_COUP_GAME_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/spiel.h"

namespace open_spiel {
namespace coup {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr int kMaxPlayers = 6;
inline constexpr int kMinPlayers = 2;
inline constexpr int kNumCardTypes = 5;
inline constexpr int kCardsPerType = 3;
inline constexpr int kTotalCards = kNumCardTypes * kCardsPerType;  // 15
inline constexpr int kNumActions = 32;
inline constexpr int kMaxCoins = 12;
inline constexpr int kForceCoupThreshold = 10;
inline constexpr int kCoupCost = 7;
inline constexpr int kAssassinateCost = 3;
inline constexpr int kStartingCoins = 2;
inline constexpr int kCardsPerPlayer = 2;

// Maximum turns (main actions) before tiebreak. Matches C engine MAX_TURNS.
inline constexpr int kMaxTurns = 200;

// History ring buffer for observation tensor.
inline constexpr int kHistoryLength = 64;
inline constexpr int kHistoryEntrySize = 4;  // floats per entry

// Observation tensor size.
// See plan.md section 6 for layout.
inline constexpr int kObservationTensorSize = 407;

// ---------------------------------------------------------------------------
// Card types
// ---------------------------------------------------------------------------

enum CardType : int {
  kDuke = 0,
  kAssassin = 1,
  kCaptain = 2,
  kAmbassador = 3,
  kContessa = 4,
};

// ---------------------------------------------------------------------------
// Phase enum (must match plan.md section 5)
// ---------------------------------------------------------------------------

enum Phase : int {
  kDeal = 0,
  kChanceRedraw = 1,
  kChanceExchange = 2,
  kMainAction = 3,
  kChallengeAction = 4,
  kBlock = 5,
  kChallengeBlock = 6,
  kLoseCard = 7,
  kExchangeDiscard = 8,
  kResolve = 9,
};

// ---------------------------------------------------------------------------
// Action indices (must match plan.md section 4)
// ---------------------------------------------------------------------------

enum ActionIndex : int {
  kIncome = 0,
  kForeignAid = 1,
  kTax = 2,
  kExchange = 3,
  kCoupPlayer0 = 4,
  // 4-9: coup -> player 0-5
  kStealPlayer0 = 10,
  // 10-15: steal -> player 0-5
  kAssassinatePlayer0 = 16,
  // 16-21: assassinate -> player 0-5
  kChallenge = 22,
  kPass = 23,
  kBlockContessa = 24,
  kBlockCaptain = 25,
  kBlockAmbassador = 26,
  kBlockDuke = 27,
  kDiscardSlot0 = 28,
  kDiscardSlot1 = 29,
  kDiscardSlot2 = 30,
  kDiscardSlot3 = 31,
};

// ---------------------------------------------------------------------------
// Per-player card info
// ---------------------------------------------------------------------------

struct CardInfo {
  int type = 0;       // CardType 0-4
  bool alive = true;  // face-down = alive
};

struct PlayerState {
  CardInfo cards[kCardsPerPlayer];
  int coins = kStartingCoins;

  bool IsAlive() const { return cards[0].alive || cards[1].alive; }
  int NumAliveCards() const {
    return (cards[0].alive ? 1 : 0) + (cards[1].alive ? 1 : 0);
  }
};

// ---------------------------------------------------------------------------
// History entry for observation tensor
// ---------------------------------------------------------------------------

struct HistoryEntry {
  int acting_player;  // 0-5
  int action;         // 0-31
  int phase;          // Phase enum
  int result;         // outcome flags
};

// ---------------------------------------------------------------------------
// CoupState
// ---------------------------------------------------------------------------

class CoupGame;

class CoupState : public State {
 public:
  explicit CoupState(std::shared_ptr<const Game> game);
  CoupState(const CoupState&) = default;
  CoupState& operator=(const CoupState&) = default;

  // --- OpenSpiel State interface ---
  Player CurrentPlayer() const override;
  std::vector<Action> LegalActions() const override;
  std::string ActionToString(Player player, Action action) const override;
  std::string ToString() const override;
  bool IsTerminal() const override;
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  void InformationStateTensor(Player player,
                              absl::Span<float> values) const override;
  std::string ObservationString(Player player) const override;
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  std::vector<std::pair<Action, double>> ChanceOutcomes() const override;

  // Test accessors
  Phase GetPhase() const { return phase_; }
  int GetFirstDiscard() const { return first_discard_; }

 protected:
  void DoApplyAction(Action action) override;

 private:
  const CoupGame* parent_game() const;
  int NumPlayers() const;

  // --- Game state ---
  std::array<PlayerState, kMaxPlayers> players_;
  std::array<int, kNumCardTypes> deck_;  // count per card type
  Phase phase_ = kDeal;
  int turn_player_ = 0;
  int active_player_ = 0;
  int pending_action_ = -1;

  // Auxiliary state
  uint8_t responded_mask_ = 0;  // 6-bit mask
  int exchange_cards_[2] = {-1, -1};  // types of drawn exchange cards
  int first_discard_ = -1;  // slot index of first exchange discard
  int blocker_ = -1;
  int block_card_ = -1;

  // Deal tracking
  int deal_count_ = 0;  // how many cards dealt so far (0..2*num_players-1)

  // Exchange draw tracking
  int exchange_draw_count_ = 0;  // 0 or 1 (which exchange card being drawn)

  // For challenge resolution: who challenged, and who was challenged
  int challenger_ = -1;
  // The card type that was claimed (for challenge resolution)
  int claimed_card_ = -1;

  // Track whether the action was blocked (for resolve)
  bool action_blocked_ = false;

  // For lose_card: who needs to lose a card
  int lose_card_player_ = -1;
  // After lose_card, what phase to go to
  Phase post_lose_card_phase_ = kResolve;
  // For challenge defense: was the challenge on a block?
  bool challenge_on_block_ = false;
  // After a failed action challenge, the action still needs to go through
  // the block phase if blockable. This flag tracks that.
  bool needs_block_ = false;

  // Turn counter (incremented each main action; game ends at kMaxTurns)
  int turn_count_ = 0;

  // Cached vectors to avoid per-call heap allocation
  mutable std::vector<Action> legal_actions_cache_;
  mutable std::vector<std::pair<Action, double>> chance_outcomes_cache_;

  // History buffer for observation
  std::vector<HistoryEntry> history_buffer_;

  // Winner cache (-1 if not terminal)
  int winner_ = -1;

  // --- Helper methods ---
  int DeckTotal() const;
  void DeckRemove(int card_type);
  void DeckAdd(int card_type);

  // Phase transitions
  void AdvanceDeal(int card_type);
  void StartMainAction();
  void AdvanceTurn();
  int NextAlivePlayer(int from) const;
  int NextResponder(int from) const;

  // Action processing
  void ApplyMainAction(Action action);
  void ApplyChallengeAction(Action action);
  void ApplyBlock(Action action);
  void ApplyChallengeBlock(Action action);
  void ApplyLoseCard(Action action);
  void ApplyExchangeDiscard(Action action);
  void ApplyResolve();

  // Utility
  bool PlayerIsAlive(int p) const;
  int CountAlivePlayers() const;
  void CheckGameOver();
  int GetTarget(int action) const;
  int GetClaimedCard(int action) const;
  bool IsChallengeable(int action) const;
  bool IsBlockable(int action) const;
  void AddHistoryEntry(int acting_player, int action, int phase, int result);

  // Legal action helpers (fill legal_actions_cache_)
  void LegalActionsMainAction() const;
  void LegalActionsChallengeAction() const;
  void LegalActionsBlock() const;
  void LegalActionsChallengeBlock() const;
  void LegalActionsLoseCard() const;
  void LegalActionsExchangeDiscard() const;

  // Observation helpers
  void FillObservationTensor(Player player,
                             absl::Span<float> values) const;
};

// ---------------------------------------------------------------------------
// CoupGame
// ---------------------------------------------------------------------------

class CoupGame : public Game {
 public:
  explicit CoupGame(const GameParameters& params);

  std::unique_ptr<State> NewInitialState() const override;
  int NumDistinctActions() const override { return kNumActions; }
  int NumPlayers() const override { return num_players_; }
  double MinUtility() const override { return -1.0; }
  double MaxUtility() const override { return 1.0; }
  absl::optional<double> UtilitySum() const override { return 0.0; }
  int MaxGameLength() const override { return 1000; }
  int MaxChanceOutcomes() const override { return kNumCardTypes; }

  std::vector<int> InformationStateTensorShape() const override {
    return {kObservationTensorSize};
  }
  std::vector<int> ObservationTensorShape() const override {
    return {kObservationTensorSize};
  }

 public:
  bool refund_on_challenge() const { return refund_on_challenge_; }

 private:
  int num_players_;
  bool refund_on_challenge_;
};

}  // namespace coup
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_COUP_GAME_H_
