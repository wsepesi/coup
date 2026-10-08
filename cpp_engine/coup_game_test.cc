// coup_game_test.cc — tests for the OpenSpiel adapter over the C engine.
//
//   1. OpenSpiel RandomSimTest for 2..6 players (legal-action masks, clone,
//      serialization round trips, observation shapes, returns vs. utility
//      bounds, MaxGameLength).
//   2. Infostate no-leak test: states that differ only in an opponent's
//      hidden cards (deal, exchange draws, exchange discard picks) give the
//      observer identical infostate strings and observations.
//   3. Public revelations (lost cards, challenge outcomes) DO reach the
//      infostate.
//   4. Zero-sum returns, including the MAX_TURNS tiebreak path.
//   5. Exhaustive depth-limited CFR-style traversal: within every infoset the
//      acting player and legal actions agree, and each player's observation
//      is a function of that player's infostate.
//   6. UndoAction / RNG-never-used checks.
//   7. Outcome-sampling MCCFR smoke run (2 players).
//   8. InformationStateTensor: shape, no-leak, function of the infostate.
//   9. ResampleFromInfostate: OpenSpiel's ResampleInfostateTest for 2..6
//      players, resampled states play on to terminal, resampled hidden cards
//      actually vary and follow the unseen-card distribution.

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "coup_game.h"
#include "open_spiel/algorithms/outcome_sampling_mccfr.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"

namespace open_spiel {
namespace coup {
namespace {

std::shared_ptr<const Game> Load(int n, bool refund = true) {
  return LoadGame("coup", {{"players", GameParameter(n)},
                           {"refund_on_challenge", GameParameter(refund)}});
}

const CoupState& AsCoup(const State& s) {
  return static_cast<const CoupState&>(s);
}

void Apply(State* s, const std::vector<Action>& actions) {
  for (Action a : actions) s->ApplyActionWithLegalityCheck(a);
}

// ---------------------------------------------------------------------------

void TestLoadAndType() {
  for (int n = kMinPlayers; n <= kMaxPlayers; ++n) {
    auto game = Load(n);
    SPIEL_CHECK_EQ(game->NumPlayers(), n);
    SPIEL_CHECK_EQ(game->NumDistinctActions(), 32);
    SPIEL_CHECK_EQ(game->MaxChanceOutcomes(), 5);
    SPIEL_CHECK_EQ(game->GetType().utility, GameType::Utility::kZeroSum);
    SPIEL_CHECK_EQ(*game->UtilitySum(), 0.0);
    SPIEL_CHECK_FLOAT_EQ(game->MinUtility(), -1.0 / (n - 1));
    SPIEL_CHECK_EQ(game->MaxUtility(), 1.0);
    SPIEL_CHECK_EQ(game->ObservationTensorSize(), COUP_OBS_SIZE);
    SPIEL_CHECK_EQ(game->MaxGameLength(), MAX_TURNS * (3 * n + 3));

    // The deal is explicit chance: 2n chance nodes before the first decision.
    auto s = game->NewInitialState();
    for (int i = 0; i < 2 * n; ++i) {
      SPIEL_CHECK_TRUE(s->IsChanceNode());
      SPIEL_CHECK_FALSE(s->IsTerminal());
      double sum = 0;
      for (auto& [a, p] : s->ChanceOutcomes()) sum += p;
      SPIEL_CHECK_FLOAT_NEAR(sum, 1.0, 1e-12);
      s->ApplyAction(s->LegalActions()[0]);
    }
    SPIEL_CHECK_EQ(s->CurrentPlayer(), 0);
  }
  std::cout << "TestLoadAndType OK" << std::endl;
}

void TestRandomSims() {
  for (int n = kMinPlayers; n <= kMaxPlayers; ++n) {
    auto game = Load(n);
    testing::RandomSimTest(*game, n == 2 ? 100 : 30, /*serialize=*/true,
                           /*verbose=*/false);
    testing::RandomSimTest(*Load(n, /*refund=*/false), 10, true, false);
    std::cout << "RandomSimTest players=" << n << " OK" << std::endl;
  }
}

// Plays random games; returns are zero-sum, winner gets +1, others
// -1/(n-1), and the winner agrees with the C engine.
void TestReturns() {
  std::mt19937 rng(1234);
  for (int n = kMinPlayers; n <= kMaxPlayers; ++n) {
    auto game = Load(n);
    for (int g = 0; g < 200; ++g) {
      auto s = game->NewInitialState();
      while (!s->IsTerminal()) {
        if (s->IsChanceNode()) {
          s->ApplyAction(SampleAction(s->ChanceOutcomes(), rng).first);
        } else {
          auto la = s->LegalActions();
          s->ApplyAction(la[std::uniform_int_distribution<int>(
              0, la.size() - 1)(rng)]);
        }
      }
      auto r = s->Returns();
      double sum = 0;
      int winners = 0;
      for (int p = 0; p < n; ++p) {
        sum += r[p];
        if (r[p] == 1.0) {
          ++winners;
          SPIEL_CHECK_EQ(p, get_winner(&AsCoup(*s).engine()));
        } else {
          SPIEL_CHECK_FLOAT_EQ(r[p], -1.0 / (n - 1));
        }
      }
      SPIEL_CHECK_EQ(winners, 1);
      SPIEL_CHECK_TRUE(std::abs(sum) < 1e-9);
    }
  }
  std::cout << "TestReturns OK" << std::endl;
}

// Both players steal from each other forever and nobody ever responds: no
// card is ever lost, so the game must end at MAX_TURNS via the tiebreak.
void TestMaxTurnsTiebreak() {
  for (int n = kMinPlayers; n <= kMaxPlayers; ++n) {
    auto game = Load(n);
    auto s = game->NewInitialState();
    int decisions = 0;
    while (!s->IsTerminal()) {
      if (s->IsChanceNode()) {
        s->ApplyAction(s->LegalActions()[0]);
        continue;
      }
      const ::Game& g = AsCoup(*s).engine();
      Action a = ACT_PASS;
      if (get_phase(&g) == PHASE_MAIN_ACTION) {
        int me = get_turn_player(&g);
        a = ACT_STEAL_P0 + (me + 1) % n;
        if (player_coins(&g, me) >= 10) a = ACT_INCOME;  // never reached
      }
      s->ApplyActionWithLegalityCheck(a);
      ++decisions;
    }
    const ::Game& g = AsCoup(*s).engine();
    SPIEL_CHECK_EQ(g.turn_count, MAX_TURNS);
    SPIEL_CHECK_LE(decisions, game->MaxGameLength());
    auto r = s->Returns();
    int w = get_winner(&g);
    SPIEL_CHECK_EQ(r[w], 1.0);
    double sum = 0;
    for (double x : r) sum += x;
    SPIEL_CHECK_TRUE(std::abs(sum) < 1e-9);
  }
  std::cout << "TestMaxTurnsTiebreak OK" << std::endl;
}

// The core no-leak property. 3 players; observer is p0. The two worlds differ
// in p1's and p2's dealt cards, p1's exchange draws and p1's exchange discard
// picks. p0's own cards and every public event are identical.
void TestInfostateNoLeak() {
  auto game = Load(3);
  // Deal order: p0 s0, p0 s1, p1 s0, p1 s1, p2 s0, p2 s1.
  auto a = game->NewInitialState();
  auto b = game->NewInitialState();
  Apply(a.get(), {DUKE, CAPTAIN, ASSASSIN, ASSASSIN, CONTESSA, AMBASSADOR});
  Apply(b.get(), {DUKE, CAPTAIN, AMBASSADOR, CONTESSA, DUKE, ASSASSIN});

  // p0: Income. p1: Exchange; p2, p0 pass the challenge window.
  Apply(a.get(), {ACT_INCOME, ACT_EXCHANGE, ACT_PASS, ACT_PASS});
  Apply(b.get(), {ACT_INCOME, ACT_EXCHANGE, ACT_PASS, ACT_PASS});
  // Exchange draws differ.
  Apply(a.get(), {CAPTAIN, DUKE});
  Apply(b.get(), {CONTESSA, CONTESSA});
  SPIEL_CHECK_EQ(a->CurrentPlayer(), 1);
  // Mid-exchange: p0 must not see the drawn cards or the first pick.
  SPIEL_CHECK_EQ(a->InformationStateString(0), b->InformationStateString(0));
  SPIEL_CHECK_EQ(a->ObservationTensor(0), b->ObservationTensor(0));
  // ... but p1 sees their own draws.
  SPIEL_CHECK_NE(a->InformationStateString(1), b->InformationStateString(1));
  SPIEL_CHECK_NE(a->ObservationTensor(1), b->ObservationTensor(1));
  // Different discard picks.
  Apply(a.get(), {ACT_DISCARD_SLOT0, ACT_DISCARD_SLOT1});
  Apply(b.get(), {ACT_DISCARD_SLOT2, ACT_DISCARD_SLOT3});
  // p2: Income.
  Apply(a.get(), {ACT_INCOME});
  Apply(b.get(), {ACT_INCOME});

  for (Player obs : {0}) {
    SPIEL_CHECK_EQ(a->InformationStateString(obs),
                   b->InformationStateString(obs));
    SPIEL_CHECK_EQ(a->ObservationTensor(obs), b->ObservationTensor(obs));
    SPIEL_CHECK_EQ(a->ObservationString(obs), b->ObservationString(obs));
    SPIEL_CHECK_EQ(a->InformationStateTensor(obs),
                   b->InformationStateTensor(obs));
  }
  // p1's own returned cards differ between the worlds and are in p1's tensor.
  SPIEL_CHECK_NE(a->InformationStateTensor(1), b->InformationStateTensor(1));
  SPIEL_CHECK_NE(a->InformationStateString(1), b->InformationStateString(1));
  SPIEL_CHECK_NE(a->InformationStateString(2), b->InformationStateString(2));
  SPIEL_CHECK_NE(a->ToString(), b->ToString());
  std::cout << "  p0 infostate: " << a->InformationStateString(0) << std::endl;
  std::cout << "  p1 infostate: " << a->InformationStateString(1) << std::endl;

  // Same check for p2 as observer of a p1 redraw after a defended challenge:
  // p1 (holding Captain in world c, Captain in d, but different other card and
  // different replacement) steals; p0 challenges and loses.
  auto c = game->NewInitialState();
  auto d = game->NewInitialState();
  Apply(c.get(), {DUKE, DUKE, CAPTAIN, ASSASSIN, CONTESSA, AMBASSADOR});
  Apply(d.get(), {DUKE, DUKE, CAPTAIN, DUKE, CONTESSA, AMBASSADOR});
  for (State* s : {c.get(), d.get()}) {
    Apply(s, {ACT_INCOME, ACT_STEAL_P0 + 2, ACT_PASS /* p2 */,
              ACT_CHALLENGE /* p0 */, ACT_DISCARD_SLOT0 /* p0 loses */});
  }
  Apply(c.get(), {CONTESSA});  // p1's replacement card
  Apply(d.get(), {ASSASSIN});
  SPIEL_CHECK_EQ(c->InformationStateString(2), d->InformationStateString(2));
  SPIEL_CHECK_EQ(c->ObservationTensor(2), d->ObservationTensor(2));
  SPIEL_CHECK_EQ(c->InformationStateTensor(2), d->InformationStateTensor(2));
  SPIEL_CHECK_EQ(c->InformationStateString(0), d->InformationStateString(0));
  SPIEL_CHECK_NE(c->InformationStateString(1), d->InformationStateString(1));
  std::cout << "TestInfostateNoLeak OK" << std::endl;
}

// Publicly revealed information must reach every player's infostate.
void TestPublicRevelations() {
  auto game = Load(2);
  auto a = game->NewInitialState();
  auto b = game->NewInitialState();
  Apply(a.get(), {DUKE, CAPTAIN, ASSASSIN, CONTESSA});
  Apply(b.get(), {DUKE, CAPTAIN, CONTESSA, ASSASSIN});
  // p0 Tax; p1 challenges and loses (p0 has Duke) -> p1 discards slot 0.
  for (State* s : {a.get(), b.get()}) {
    Apply(s, {ACT_TAX, ACT_CHALLENGE, ACT_DISCARD_SLOT0});
  }
  // p1's slot-0 card is now face up and differs between worlds.
  SPIEL_CHECK_NE(a->InformationStateString(0), b->InformationStateString(0));
  SPIEL_CHECK_NE(a->ObservationTensor(0), b->ObservationTensor(0));

  // Challenge outcome: p0 claims Tax with / without a Duke.
  auto c = game->NewInitialState();
  auto d = game->NewInitialState();
  Apply(c.get(), {DUKE, CAPTAIN, ASSASSIN, CONTESSA});
  Apply(d.get(), {AMBASSADOR, CAPTAIN, ASSASSIN, CONTESSA});
  Apply(c.get(), {ACT_TAX, ACT_CHALLENGE});
  Apply(d.get(), {ACT_TAX, ACT_CHALLENGE});
  SPIEL_CHECK_NE(c->InformationStateString(1), d->InformationStateString(1));
  SPIEL_CHECK_NE(c->CurrentPlayer(), d->CurrentPlayer());
  std::cout << "TestPublicRevelations OK" << std::endl;
}

// Exhaustive depth-limited traversal of the full game tree, as a CFR
// implementation would do it. Checks for every reached history h and every
// player p:
//   - all histories sharing (p, InformationStateString(p)) where p acts have
//     the same legal actions;
//   - ObservationTensor(p) / ObservationString(p) are functions of
//     InformationStateString(p) (otherwise the infostate is missing
//     something the player can see).
struct TraversalStats {
  int64_t nodes = 0, decision_nodes = 0, terminal = 0;
  std::map<std::string, std::vector<Action>> legal;  // key: infostate
  std::map<std::string, std::vector<float>> obs;     // key: "p|infostate"
  std::map<std::string, std::vector<float>> info_tensor;
  std::map<std::string, std::string> obs_str;
  double reach_sum_at_frontier = 0;
};

void Traverse(const State& s, int depth, double reach, TraversalStats* st) {
  ++st->nodes;
  const int n = s.NumPlayers();
  for (Player p = 0; p < n; ++p) {
    std::string info = s.InformationStateString(p);
    auto obs = s.ObservationTensor(p);
    auto [it, inserted] = st->obs.emplace(info, obs);
    if (!inserted && it->second != obs) {
      SpielFatalError(absl::StrCat("Observation tensor differs within infoset ",
                                   info, "\nstate:\n", s.ToString()));
    }
    auto itensor = s.InformationStateTensor(p);
    auto [it3, ins3] = st->info_tensor.emplace(info, itensor);
    if (!ins3 && it3->second != itensor) {
      SpielFatalError(absl::StrCat("Info-state tensor differs within infoset ",
                                   info, "\nstate:\n", s.ToString()));
    }
    auto ostr = s.ObservationString(p);
    auto [it2, ins2] = st->obs_str.emplace(info, ostr);
    if (!ins2 && it2->second != ostr) {
      SpielFatalError(absl::StrCat("Observation string differs within infoset ",
                                   info, "\n", it2->second, "\nvs\n", ostr));
    }
  }
  if (s.IsTerminal()) {
    ++st->terminal;
    st->reach_sum_at_frontier += reach;
    return;
  }
  if (s.IsChanceNode()) {
    for (auto& [a, p] : s.ChanceOutcomes()) {
      Traverse(*s.Child(a), depth, reach * p, st);
    }
    return;
  }
  ++st->decision_nodes;
  Player cur = s.CurrentPlayer();
  std::string key = s.InformationStateString(cur);
  auto la = s.LegalActions();
  auto [it, inserted] = st->legal.emplace(key, la);
  if (!inserted) SPIEL_CHECK_EQ(it->second, la);
  if (depth == 0) {
    st->reach_sum_at_frontier += reach;
    return;
  }
  // Uniform policy: reach probabilities must still sum to 1 at the frontier.
  for (Action a : la) Traverse(*s.Child(a), depth - 1, reach / la.size(), st);
}

void TestCfrTraversal() {
  for (auto [n, depth] : std::vector<std::pair<int, int>>{{2, 4}, {3, 2}}) {
    auto game = Load(n);
    TraversalStats st;
    Traverse(*game->NewInitialState(), depth, 1.0, &st);
    SPIEL_CHECK_FLOAT_NEAR(st.reach_sum_at_frontier, 1.0, 1e-9);
    std::cout << "TestCfrTraversal players=" << n << " depth=" << depth
              << ": nodes=" << st.nodes << " decision=" << st.decision_nodes
              << " infosets(acting)=" << st.legal.size() << " OK" << std::endl;
  }
}

void TestUndoAndClone() {
  std::mt19937 rng(7);
  auto game = Load(4);
  auto s = game->NewInitialState();
  std::vector<std::unique_ptr<State>> snapshots;
  while (!s->IsTerminal() && snapshots.size() < 300) {
    snapshots.push_back(s->Clone());
    Action a = s->IsChanceNode()
                   ? SampleAction(s->ChanceOutcomes(), rng).first
                   : s->LegalActions()[std::uniform_int_distribution<int>(
                         0, s->LegalActions().size() - 1)(rng)];
    s->ApplyAction(a);
  }
  for (int i = static_cast<int>(snapshots.size()) - 1; i >= 0; --i) {
    s->UndoAction(snapshots[i]->CurrentPlayer(), s->History().back());
    SPIEL_CHECK_EQ(s->ToString(), snapshots[i]->ToString());
    SPIEL_CHECK_EQ(s->History(), snapshots[i]->History());
    for (Player p = 0; p < 4; ++p) {
      SPIEL_CHECK_EQ(s->InformationStateString(p),
                     snapshots[i]->InformationStateString(p));
      SPIEL_CHECK_EQ(s->ObservationTensor(p),
                     snapshots[i]->ObservationTensor(p));
    }
  }
  std::cout << "TestUndoAndClone OK" << std::endl;
}

// The adapter must never consume the RNG embedded in the C Game struct.
void TestRngNeverUsed() {
  ::Game fresh;
  game_init(&fresh, 2, 0, 0);
  std::mt19937 rng(99);
  auto game = Load(2);
  for (int g = 0; g < 50; ++g) {
    auto s = game->NewInitialState();
    while (!s->IsTerminal()) {
      auto la = s->LegalActions();
      s->ApplyAction(s->IsChanceNode()
                         ? SampleAction(s->ChanceOutcomes(), rng).first
                         : la[std::uniform_int_distribution<int>(
                               0, la.size() - 1)(rng)]);
    }
    SPIEL_CHECK_EQ(
        std::memcmp(&AsCoup(*s).engine().rng, &fresh.rng, sizeof(fresh.rng)),
        0);
  }
  std::cout << "TestRngNeverUsed OK" << std::endl;
}

void TestMccfrSmoke() {
  auto game = Load(2);
  algorithms::OutcomeSamplingMCCFRSolver solver(*game, /*epsilon=*/0.6,
                                                /*seed=*/1);
  for (int i = 0; i < 300; ++i) solver.RunIteration();
  const auto& table = solver.InfoStateValuesTable();
  SPIEL_CHECK_GT(table.size(), 0u);
  auto policy = solver.AveragePolicy();
  // Every stored infoset yields a normalised distribution over its legal
  // actions.
  int checked = 0;
  for (const auto& [info, values] : table) {
    double sum = 0;
    for (auto& [a, p] : policy->GetStatePolicy(info)) {
      SPIEL_CHECK_GE(p, 0);
      sum += p;
    }
    SPIEL_CHECK_FLOAT_NEAR(sum, 1.0, 1e-6);
    if (++checked >= 2000) break;
  }
  std::cout << "TestMccfrSmoke OK (" << table.size() << " infosets after 300 "
            << "iterations)" << std::endl;
}


void TestInfoStateTensor() {
  auto game = Load(3);
  SPIEL_CHECK_TRUE(game->GetType().provides_information_state_tensor);
  SPIEL_CHECK_EQ(game->InformationStateTensorSize(), kInfoStateTensorSize);
  auto s = game->NewInitialState();
  Apply(s.get(), {DUKE, CAPTAIN, ASSASSIN, ASSASSIN, CONTESSA, AMBASSADOR});
  // p0 Tax; p1 challenges and loses (p0 holds Duke); p1 loses card 0; p0
  // redraws slot 0.
  Apply(s.get(), {ACT_TAX, ACT_CHALLENGE, ACT_DISCARD_SLOT0, CONTESSA});
  auto t = s->InformationStateTensor(2);  // observer p2
  SPIEL_CHECK_EQ(static_cast<int>(t.size()), kInfoStateTensorSize);
  // Observation prefix equals the observation tensor.
  auto o = s->ObservationTensor(2);
  for (int i = 0; i < kObservationTensorSize; ++i) SPIEL_CHECK_EQ(t[i], o[i]);
  // p2's relative seats: p2 -> 0, p0 -> 1, p1 -> 2.
  const float* seat_p0 = t.data() + kObservationTensorSize + 1 * kInfoSeatSize;
  const float* seat_p1 = t.data() + kObservationTensorSize + 2 * kInfoSeatSize;
  SPIEL_CHECK_EQ(seat_p0[0], 1.0f);           // one Tax claim
  SPIEL_CHECK_EQ(seat_p0[1], 0.0f);
  SPIEL_CHECK_EQ(seat_p0[33 + DUKE], 1.0f);   // Duke proven
  SPIEL_CHECK_EQ(seat_p1[24], 1.0f);          // one challenge made
  SPIEL_CHECK_EQ(seat_p1[30], 1.0f);          // ... and lost
  SPIEL_CHECK_EQ(seat_p1[27], 0.0f);
  std::cout << "TestInfoStateTensor OK" << std::endl;
}

void TestResample() {
  for (int n = 2; n <= 6; ++n) {
    testing::ResampleInfostateTest(*Load(n), n == 2 ? 30 : 10);
    std::cout << "ResampleInfostateTest players=" << n << " OK" << std::endl;
  }

  // Resampled states are full, playable games.
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> unif(0.0, 1.0);
  auto sampler = [&]() { return unif(rng); };
  for (int n : {2, 4, 6}) {
    auto game = Load(n);
    for (int sim = 0; sim < 20; ++sim) {
      auto s = game->NewInitialState();
      while (!s->IsTerminal()) {
        if (!s->IsChanceNode() && rng() % 7 == 0) {
          Player p = rng() % n;
          auto r = s->ResampleFromInfostate(p, sampler);
          SPIEL_CHECK_EQ(r->InformationStateString(p),
                         s->InformationStateString(p));
          SPIEL_CHECK_EQ(r->InformationStateTensor(p),
                         s->InformationStateTensor(p));
          SPIEL_CHECK_EQ(r->History().size(), s->History().size());
          while (!r->IsTerminal()) {
            auto la = r->LegalActions();
            r->ApplyAction(la[rng() % la.size()]);
          }
        }
        auto la = s->LegalActions();
        s->ApplyAction(la[rng() % la.size()]);
      }
    }
  }

  // Distribution: 2 players right after the deal, p0 holds Duke+Duke. p1's
  // first card is uniform over the 13 unseen cards: Duke 1/13, others 3/13.
  {
    auto s = Load(2)->NewInitialState();
    Apply(s.get(), {DUKE, DUKE, CAPTAIN, CAPTAIN});
    int counts[kNumCardTypes] = {};
    const int kSamples = 20000;
    for (int i = 0; i < kSamples; ++i) {
      auto r = s->ResampleFromInfostate(0, sampler);
      const ::Game& g = AsCoup(*r).engine();
      SPIEL_CHECK_EQ(player_card0_type(&g, 0), DUKE);
      SPIEL_CHECK_EQ(player_card1_type(&g, 0), DUKE);
      ++counts[player_card0_type(&g, 1)];
    }
    for (int c = 0; c < kNumCardTypes; ++c) {
      const double expect = (c == DUKE ? 1.0 : 3.0) / 13.0;
      SPIEL_CHECK_FLOAT_NEAR(counts[c] / double(kSamples), expect, 0.015);
    }
  }

  // Constraints: p1 was caught bluffing Duke, so no resample may give p1 a
  // Duke; p0 sees p1 reveal Assassin.
  {
    auto s = Load(2)->NewInitialState();
    Apply(s.get(), {CAPTAIN, CONTESSA, ASSASSIN, AMBASSADOR});
    Apply(s.get(), {ACT_INCOME, ACT_TAX, ACT_CHALLENGE, ACT_DISCARD_SLOT0});
    SPIEL_CHECK_EQ(s->CurrentPlayer(), 0);  // p0's turn, p1 has one card
    for (int i = 0; i < 2000; ++i) {
      auto r = s->ResampleFromInfostate(0, sampler);
      const ::Game& g = AsCoup(*r).engine();
      SPIEL_CHECK_EQ(player_card0_type(&g, 1), ASSASSIN);
      SPIEL_CHECK_FALSE(player_card0_alive(&g, 1));
      SPIEL_CHECK_NE(player_card1_type(&g, 1), DUKE);
    }
  }

  // Timing over long 6-player histories.
  {
    auto game = Load(6);
    int64_t calls = 0;
    auto start = std::chrono::steady_clock::now();
    for (int sim = 0; sim < 20; ++sim) {
      auto s = game->NewInitialState();
      while (!s->IsTerminal()) {
        if (!s->IsChanceNode()) {
          s->ResampleFromInfostate(s->CurrentPlayer(), sampler);
          ++calls;
        }
        auto la = s->LegalActions();
        s->ApplyAction(la[rng() % la.size()]);
      }
    }
    double secs = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - start).count();
    std::cout << "Resample 6p: " << calls << " calls, "
              << 1e6 * secs / calls << " us/call" << std::endl;
  }
  std::cout << "TestResample OK" << std::endl;
}

}  // namespace
}  // namespace coup
}  // namespace open_spiel

int main() {
  using namespace open_spiel::coup;
  TestLoadAndType();
  TestInfostateNoLeak();
  TestPublicRevelations();
  TestReturns();
  TestMaxTurnsTiebreak();
  TestRngNeverUsed();
  TestUndoAndClone();
  TestCfrTraversal();
  TestRandomSims();
  TestMccfrSmoke();
  TestInfoStateTensor();
  TestResample();
  std::cout << "All coup_game tests passed." << std::endl;
  return 0;
}
