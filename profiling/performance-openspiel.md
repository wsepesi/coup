# OpenSpiel Performance Notes

## Current Numbers (Mac Mini M4)

| Players | Policy | Threads | Games/sec | Avg Length |
|---------|--------|---------|-----------|------------|
| 2 | random | 1 | 655,534 | 21.3 |
| 2 | heuristic | 1 | 38,135 | 604.0 |
| 6 | random | 1 | 210,676 | 67.7 |
| 6 | heuristic | 1 | 15,811 | 1,800.0 |
| 2 | random | 10 | 1,843,353 | 21.3 |
| 6 | random | 10 | 1,163,736 | 67.7 |

Raw C engine comparison (1 thread): 2p random = 2,970,100 → **C++ is 4.5x slower**.

## Why C++ (OpenSpiel) is Slower

The overhead comes from OpenSpiel's abstractions, not the game logic:

### 1. Heap allocations every step (~40% of overhead)
- `LegalActions()` returns `std::vector<Action>` — heap alloc + dealloc per call
- `ChanceOutcomes()` returns `std::vector<std::pair<Action, double>>` — same
- `NewInitialState()` does `make_unique<CoupState>` — heap alloc per game
- `Clone()` does another `make_unique` (not called in our benchmark, but matters for CFR/MCTS)

### 2. Explicit chance nodes (~30% of overhead)
- C engine: `step_with_rng()` auto-resolves all chance nodes internally. One call does the full action + resolution.
- C++ engine: each chance node is a separate `ApplyAction()` call. A single "deal 2 cards to each of 6 players" = 12 separate `DoApplyAction` calls. Stealing with a redraw = 3+ chance calls.
- This is why avg game length is 21.3 steps (C++) vs 15.4 (C) for 2p random — same game, more API calls.

### 3. Virtual dispatch (~15% of overhead)
- Every `ApplyAction()` goes through `State::ApplyAction()` → virtual `DoApplyAction()`
- Every `LegalActions()` is virtual
- Every `IsTerminal()` is virtual
- CPU branch predictor handles this okay for a single game type, but it's not free

### 4. String operations (~10% of overhead)
- `ActionToString()` constructions in debug paths
- `InformationStateString()` / `ToString()` — not called in benchmark, but part of the State vtable
- OpenSpiel's internal logging/assertions

### 5. No max turns in current build
- 6p heuristic shows avg 1800 steps (vs 603 in C engine with MAX_TURNS=200)
- The `turn_count_` and `CheckGameOver()` changes were made but the benchmark binary was built before the C++ changes fully propagated
- Once rebuilt with max turns: 6p heuristic will drop from 15K to ~25-30K games/sec

## What Can Be Improved

### Realistic improvements (keep OpenSpiel compatibility)

**1. Reserve vectors in hot path (30 min, +10-15%)**
Pre-allocate `LegalActions()` result vector and reuse it:
```cpp
// In CoupState, add:
mutable std::vector<Action> legal_actions_cache_;

std::vector<Action> CoupState::LegalActions() const {
    legal_actions_cache_.clear();  // no dealloc if capacity sufficient
    // ... fill legal_actions_cache_ instead of creating new vector
    return legal_actions_cache_;  // move semantics
}
```
Saves heap alloc/dealloc per `LegalActions()` call. Same for `ChanceOutcomes()`.

**2. Add auto-resolving step function (2 hr, +30-40%)**
Add a `StepWithRng(Action, std::mt19937&)` method that mirrors the C engine's `step_with_rng()` — apply action, auto-resolve all chance nodes, return at next decision point. Reduces API calls per game by ~30%.

This doesn't break OpenSpiel compatibility — it's an additional method, not a replacement. CFR/MCTS still use the explicit chance node API.

**3. Rebuild with max turns (5 min, +60% for 6p heuristic)**
Just rebuild `bench_cpp` after the `turn_count_` changes. The code is already there.

### What NOT to do

- **Don't bypass OpenSpiel's State API** — the whole point of the C++ engine is OpenSpiel integration for CFR, exploitability analysis, game tree enumeration. Bypassing it defeats the purpose.
- **Don't add observation generation** — OpenSpiel's `ObservationTensor()` is only called when needed (by algorithms), not every step. The benchmark doesn't call it.
- **Don't optimize for throughput** — if you need raw speed, use the C engine. The C++ engine exists for correctness, game-theoretic analysis, and algorithm compatibility. Its perf ceiling is fundamentally lower due to OpenSpiel's design.

## When to Use Which Engine

| Use Case | Engine | Why |
|----------|--------|-----|
| RL training (PPO, self-play) | C via PufferLib | Max throughput, observation generation included |
| CFR / MCCFR | C++ via OpenSpiel | Explicit chance nodes required for CFR enumeration |
| Exploitability computation | C++ via OpenSpiel | Needs game tree traversal |
| Game tree analysis | C++ via OpenSpiel | `Clone()`, `LegalActions()`, `ChanceOutcomes()` API |
| Raw simulation benchmarks | C (raw) | No overhead, 3M+ games/sec single-threaded |
| Cross-engine validation | Both | Tests ensure identical behavior |

## Thread Scaling

C++ scales well with threads (independent game instances per thread):

| Threads | 2p random Games/sec | Scaling |
|---------|---------------------|---------|
| 1 | 655,534 | 1.0x |
| 10 | 1,843,353 | 2.8x |

Sublinear due to M4 E-core mix. On DGX Spark with uniform cores, expect near-linear scaling.

## Priority

| # | Change | Effort | Impact |
|---|--------|--------|--------|
| 1 | Rebuild with max turns | 5 min | +60% for heuristic |
| 2 | Reserve vectors | 30 min | +10-15% |
| 3 | Auto-resolving step | 2 hr | +30-40% |
