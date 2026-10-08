// mccfr_example.cc — outcome-sampling MCCFR on 2-player Coup.
//
// Usage: ./mccfr_example [iterations=20000] [refund_on_challenge=1]
//
// Runs tabular outcome-sampling MCCFR (OpenSpiel) against the C engine via
// the adapter and prints the average policy at player 0's opening decision
// for every possible starting hand. This is a demonstration, not a solve:
// full Coup has far too many infosets for tabular CFR to converge.

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

#include "coup_game.h"
#include "open_spiel/algorithms/outcome_sampling_mccfr.h"
#include "open_spiel/spiel.h"

int main(int argc, char** argv) {
  using namespace open_spiel;
  const int iters = argc > 1 ? std::atoi(argv[1]) : 20000;
  const bool refund = argc > 2 ? std::atoi(argv[2]) != 0 : true;
  auto game = LoadGame("coup", {{"players", GameParameter(2)},
                                {"refund_on_challenge", GameParameter(refund)}});

  algorithms::OutcomeSamplingMCCFRSolver solver(*game, /*epsilon=*/0.6,
                                                /*seed=*/42);
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 1; i <= iters; ++i) {
    solver.RunIteration();
    if (i % 5000 == 0 || i == iters) {
      double secs = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - t0).count();
      std::cout << "iter " << i << "  infosets "
                << solver.InfoStateValuesTable().size() << "  " << std::fixed
                << std::setprecision(1) << secs << "s" << std::endl;
    }
  }

  // Player 0's opening decision for each unordered starting hand.
  auto policy = solver.AveragePolicy();
  std::set<std::string> seen;
  std::cout << "\nAverage policy at p0's first decision:\n";
  for (int c0 = 0; c0 < 5; ++c0) {
    for (int c1 = c0; c1 < 5; ++c1) {
      auto s = game->NewInitialState();
      s->ApplyAction(c0);
      s->ApplyAction(c1);
      // Opponent's cards are hidden from p0; any legal deal gives the same
      // infostate for p0.
      while (s->IsChanceNode()) s->ApplyAction(s->LegalActions()[0]);
      std::string info = s->InformationStateString(0);
      if (!seen.insert(info).second) continue;
      std::cout << coup::CardName(c0) << "+" << coup::CardName(c1) << ":";
      for (auto& [a, p] : policy->GetStatePolicy(info)) {
        if (p >= 0.05) {
          std::cout << " " << coup::ActionName(a) << "=" << std::setprecision(2)
                    << p;
        }
      }
      std::cout << "\n";
    }
  }
  return 0;
}
