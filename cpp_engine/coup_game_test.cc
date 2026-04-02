// coup_game_test.cc -- Tests for the Coup OpenSpiel game implementation.
// Uses OpenSpiel's test utilities and basic assertions.

#include "open_spiel/spiel.h"
#include "open_spiel/tests/basic_tests.h"
#include "coup_game.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace open_spiel {
namespace coup {
namespace {

// Helper: sample a chance action from the given state using the provided rng.
Action SampleChance(const State& state, std::mt19937& rng) {
  auto outcomes = state.ChanceOutcomes();
  std::vector<double> probs;
  std::vector<Action> actions;
  for (auto& [a, p] : outcomes) {
    actions.push_back(a);
    probs.push_back(p);
  }
  std::discrete_distribution<> dist(probs.begin(), probs.end());
  return actions[dist(rng)];
}

// Helper: advance past all chance nodes (e.g. the initial deal).
void AdvancePastChance(State& state, std::mt19937& rng) {
  while (state.IsChanceNode()) {
    state.ApplyAction(SampleChance(state, rng));
  }
}

// ---------------------------------------------------------------------------
// 1. Game loads from registry
// ---------------------------------------------------------------------------
void LoadGameTest() {
  auto game = LoadGame("coup");
  assert(game != nullptr);
  assert(game->NumPlayers() == 2);  // default

  auto game6 = LoadGame("coup(players=6)");
  assert(game6 != nullptr);
  assert(game6->NumPlayers() == 6);

  // Verify other game properties.
  assert(game->NumDistinctActions() == kNumActions);
  assert(game->MinUtility() == -1.0);
  assert(game->MaxUtility() == 1.0);
  assert(game->MaxChanceOutcomes() == kNumCardTypes);

  std::cout << "LoadGameTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 2. Random simulation test (runs without crashing)
// ---------------------------------------------------------------------------
void RandomSimulationTest() {
  auto game2 = LoadGame("coup(players=2)");
  testing::RandomSimTest(*game2, 100);

  auto game6 = LoadGame("coup(players=6)");
  testing::RandomSimTest(*game6, 50);

  std::cout << "RandomSimulationTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 3. Chance outcomes validity
// ---------------------------------------------------------------------------
void ChanceOutcomesTest() {
  auto game = LoadGame("coup(players=2)");
  auto state = game->NewInitialState();

  // Initial state should be a chance node (DEAL phase).
  assert(state->IsChanceNode());

  auto outcomes = state->ChanceOutcomes();
  assert(!outcomes.empty());

  double total = 0.0;
  for (auto& [action, prob] : outcomes) {
    assert(prob > 0.0);
    assert(action >= 0 && action < kNumCardTypes);
    total += prob;
  }
  assert(std::abs(total - 1.0) < 1e-9);

  std::cout << "ChanceOutcomesTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Games reach terminal for all player counts
// ---------------------------------------------------------------------------
void TerminalReachTest() {
  std::mt19937 rng(42);

  for (int np : {2, 3, 4, 5, 6}) {
    auto game = LoadGame("coup(players=" + std::to_string(np) + ")");
    auto state = game->NewInitialState();
    int steps = 0;

    while (!state->IsTerminal() && steps < 10000) {
      if (state->IsChanceNode()) {
        state->ApplyAction(SampleChance(*state, rng));
      } else {
        auto legal = state->LegalActions();
        assert(!legal.empty());
        std::uniform_int_distribution<> dist(0, legal.size() - 1);
        state->ApplyAction(legal[dist(rng)]);
      }
      steps++;
    }

    assert(state->IsTerminal());

    auto returns = state->Returns();
    assert(static_cast<int>(returns.size()) == np);

    // Exactly one player should have a positive return (the winner).
    int winners = 0;
    for (double r : returns) {
      if (r > 0.0) winners++;
    }
    assert(winners == 1);

    std::cout << "TerminalReachTest(" << np << "p) PASSED ("
              << steps << " steps)" << std::endl;
  }
}

// ---------------------------------------------------------------------------
// 5. InformationStateString differs for players with different hands
// ---------------------------------------------------------------------------
void InfoStateStringTest() {
  auto game = LoadGame("coup(players=2)");
  auto state = game->NewInitialState();
  std::mt19937 rng(42);

  // Deal all cards.
  AdvancePastChance(*state, rng);

  std::string info0 = state->InformationStateString(0);
  std::string info1 = state->InformationStateString(1);

  // Different players should have different information state strings
  // because they see different private cards.
  assert(!info0.empty());
  assert(!info1.empty());
  assert(info0 != info1);

  std::cout << "InfoStateStringTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 6. InformationStateTensor shape and content
// ---------------------------------------------------------------------------
void InfoStateTensorTest() {
  auto game = LoadGame("coup(players=2)");
  auto state = game->NewInitialState();
  std::mt19937 rng(42);

  // Deal all cards.
  AdvancePastChance(*state, rng);

  auto tensor_shape = game->InformationStateTensorShape();
  assert(!tensor_shape.empty());

  int expected_size = 1;
  for (int d : tensor_shape) {
    expected_size *= d;
  }
  assert(expected_size == kObservationTensorSize);

  std::vector<float> tensor(expected_size, 0.0f);
  state->InformationStateTensor(0, absl::MakeSpan(tensor));

  // Check that tensor is not all zeros (player should at least see own cards
  // and coins).
  bool has_nonzero = false;
  for (float v : tensor) {
    if (v != 0.0f) {
      has_nonzero = true;
      break;
    }
  }
  assert(has_nonzero);

  // Tensors for different players should differ (different private info).
  std::vector<float> tensor1(expected_size, 0.0f);
  state->InformationStateTensor(1, absl::MakeSpan(tensor1));
  assert(tensor != tensor1);

  std::cout << "InfoStateTensorTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 7. Action semantics verification
// ---------------------------------------------------------------------------
void ActionSemanticsTest() {
  auto game = LoadGame("coup(players=2)");
  assert(game->NumDistinctActions() == kNumActions);

  auto state = game->NewInitialState();
  std::mt19937 rng(42);

  // Deal cards.
  AdvancePastChance(*state, rng);

  // At kMainAction, Income (action 0) should always be legal.
  auto legal = state->LegalActions();
  bool has_income = false;
  for (auto a : legal) {
    if (a == kIncome) has_income = true;
  }
  assert(has_income);

  // Foreign Aid (action 1) should also be legal at the start.
  bool has_foreign_aid = false;
  for (auto a : legal) {
    if (a == kForeignAid) has_foreign_aid = true;
  }
  assert(has_foreign_aid);

  // ActionToString should produce non-empty strings for all legal actions.
  Player player = state->CurrentPlayer();
  for (auto a : legal) {
    std::string action_str = state->ActionToString(player, a);
    assert(!action_str.empty());
  }

  std::cout << "ActionSemanticsTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 8. Clone produces independent copy
// ---------------------------------------------------------------------------
void CloneTest() {
  auto game = LoadGame("coup(players=2)");
  auto state = game->NewInitialState();
  std::mt19937 rng(42);

  // Deal cards.
  AdvancePastChance(*state, rng);

  // Clone the state.
  auto cloned = state->Clone();

  // They should produce the same ToString.
  assert(state->ToString() == cloned->ToString());

  // Apply an action to the original; clone should be unchanged.
  auto legal = state->LegalActions();
  assert(!legal.empty());
  std::string cloned_str_before = cloned->ToString();
  state->ApplyAction(legal[0]);
  assert(cloned->ToString() == cloned_str_before);
  assert(state->ToString() != cloned->ToString());

  std::cout << "CloneTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 9. ObservationString and ObservationTensor
// ---------------------------------------------------------------------------
void ObservationTest() {
  auto game = LoadGame("coup(players=2)");
  auto state = game->NewInitialState();
  std::mt19937 rng(42);

  AdvancePastChance(*state, rng);

  // ObservationString should be non-empty.
  std::string obs0 = state->ObservationString(0);
  assert(!obs0.empty());

  // ObservationTensor should have the right size and not be all zeros.
  auto obs_shape = game->ObservationTensorShape();
  int obs_size = 1;
  for (int d : obs_shape) obs_size *= d;

  std::vector<float> obs_tensor(obs_size, 0.0f);
  state->ObservationTensor(0, absl::MakeSpan(obs_tensor));

  bool has_nonzero = false;
  for (float v : obs_tensor) {
    if (v != 0.0f) {
      has_nonzero = true;
      break;
    }
  }
  assert(has_nonzero);

  std::cout << "ObservationTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 10. Exchange discard: slot 3 never offered as first discard
// ---------------------------------------------------------------------------
void ExchangeDiscardCanonicalTest() {
  // The canonical ordering constraint requires second_slot > first_slot.
  // Therefore slot 3 (the highest) must never appear in the first discard
  // mask, since no slot above it exists for the second discard.
  auto game = LoadGame("coup(players=2)");
  std::mt19937 rng(42);

  // Run many games and check every ExchangeDiscard state we encounter.
  for (int trial = 0; trial < 200; trial++) {
    auto state = game->NewInitialState();
    AdvancePastChance(*state, rng);

    int steps = 0;
    while (!state->IsTerminal() && steps < 500) {
      if (state->IsChanceNode()) {
        state->ApplyAction(SampleChance(*state, rng));
        continue;
      }

      auto legal = state->LegalActions();
      assert(!legal.empty());

      // If we're in ExchangeDiscard, verify the constraint
      auto* coup_state = dynamic_cast<CoupState*>(state.get());
      if (coup_state && coup_state->GetPhase() == kExchangeDiscard) {
        if (coup_state->GetFirstDiscard() < 0) {
          // First discard: slot 3 must not be offered
          for (Action a : legal) {
            assert(a != kDiscardSlot3);
          }
        }
        // Pick first legal action
        Action first = legal[0];
        state->ApplyAction(first);

        // After first discard, second discard must have options
        if (!state->IsTerminal() && !state->IsChanceNode()) {
          auto legal2 = state->LegalActions();
          assert(!legal2.empty());
        }
      } else {
        // Pick random legal action
        std::uniform_int_distribution<> dist(0, legal.size() - 1);
        state->ApplyAction(legal[dist(rng)]);
      }
      steps++;
    }
  }

  std::cout << "ExchangeDiscardCanonicalTest PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 11. Exchange discard: every first pick leaves a valid second pick
// ---------------------------------------------------------------------------
void ExchangeDiscardNoDeadlockTest() {
  // For every reachable ExchangeDiscard state, try every valid first discard
  // and verify the second discard mask is non-empty.
  auto game = LoadGame("coup(players=2)");
  std::mt19937 rng(99);

  int exchange_states_checked = 0;

  for (int trial = 0; trial < 300; trial++) {
    auto state = game->NewInitialState();
    AdvancePastChance(*state, rng);

    int steps = 0;
    while (!state->IsTerminal() && steps < 500) {
      if (state->IsChanceNode()) {
        state->ApplyAction(SampleChance(*state, rng));
        continue;
      }

      auto legal = state->LegalActions();
      if (legal.empty()) break;

      auto* coup_state = dynamic_cast<CoupState*>(state.get());
      if (coup_state && coup_state->GetPhase() == kExchangeDiscard &&
          coup_state->GetFirstDiscard() < 0) {
        // First discard: try every option and check second discard
        for (Action first_action : legal) {
          auto cloned = state->Clone();
          cloned->ApplyAction(first_action);
          if (!cloned->IsTerminal() && !cloned->IsChanceNode()) {
            auto second_legal = cloned->LegalActions();
            assert(!second_legal.empty());
          }
        }
        exchange_states_checked++;
      }

      std::uniform_int_distribution<> dist(0, legal.size() - 1);
      state->ApplyAction(legal[dist(rng)]);
      steps++;
    }
  }

  assert(exchange_states_checked > 0);
  std::cout << "ExchangeDiscardNoDeadlockTest PASSED ("
            << exchange_states_checked << " states checked)" << std::endl;
}

}  // namespace
}  // namespace coup
}  // namespace open_spiel

int main(int /*argc*/, char** /*argv*/) {
  open_spiel::coup::LoadGameTest();
  open_spiel::coup::RandomSimulationTest();
  open_spiel::coup::ChanceOutcomesTest();
  open_spiel::coup::TerminalReachTest();
  open_spiel::coup::InfoStateStringTest();
  open_spiel::coup::InfoStateTensorTest();
  open_spiel::coup::ActionSemanticsTest();
  open_spiel::coup::CloneTest();
  open_spiel::coup::ObservationTest();
  open_spiel::coup::ExchangeDiscardCanonicalTest();
  open_spiel::coup::ExchangeDiscardNoDeadlockTest();

  std::cout << "\nAll tests PASSED!" << std::endl;
  return 0;
}
