# Coup Engine Benchmark Results

## Machine: Mac Mini M4
- **OS**: Darwin (arm64)
- **CPU**: Apple M4
- **Cores**: 10 (4P + 6E)
- **Compiler**: Apple clang 21.0.0
- **Date**: 2026-04-02
- **Duration**: 10s per benchmark
- **OpenMP**: no (pthreads fallback)
- **Max turns**: 200 (tiebreak: most cards, then most coins)

## Raw C Engine

| Players | Policy | Threads | Games/sec | Avg Game Length |
|---------|--------|---------|-----------|-----------------|
| 2 | random | 1 | 2,970,100 | 15.4 |
| 2 | random | 2 | 5,861,211 | 15.4 |
| 2 | random | 4 | 11,355,529 | 15.4 |
| 2 | random | 10 | 20,996,726 | 15.4 |
| 2 | heuristic | 1 | 1,031,331 | 106.5 |
| 2 | heuristic | 2 | 2,022,373 | 106.5 |
| 2 | heuristic | 4 | 3,956,130 | 106.4 |
| 2 | heuristic | 10 | 6,708,963 | 106.4 |
| 6 | random | 1 | 774,975 | 51.1 |
| 6 | random | 2 | 1,530,201 | 51.1 |
| 6 | random | 4 | 2,949,575 | 51.1 |
| 6 | random | 10 | 5,475,642 | 51.1 |
| 6 | heuristic | 1 | 101,133 | 602.8 |
| 6 | heuristic | 2 | 199,561 | 602.7 |
| 6 | heuristic | 4 | 384,921 | 602.6 |
| 6 | heuristic | 10 | 686,967 | 602.7 |

Thread scaling: ~2x per doubling up to 4 cores, ~6.7x at 10 cores (some E-core penalty).

## PufferLib (realistic training loop)

Full observation generation (407 floats + 32-float action mask) every step.
C-side action selection (simulates neural net policy output).

| Players | Policy | Num Envs | Games/sec | Avg Game Length |
|---------|--------|----------|-----------|-----------------|
| 2 | random | 100 | 739,396 | 15.4 |
| 2 | random | 1,000 | 1,036,536 | 15.4 |
| 2 | random | 10,000 | 783,439 | 15.4 |
| 2 | heuristic | 100 | 104,110 | 106.3 |
| 2 | heuristic | 1,000 | 152,168 | 106.4 |
| 2 | heuristic | 10,000 | 114,055 | 105.5 |
| 6 | random | 100 | 168,218 | 51.1 |
| 6 | random | 1,000 | 222,642 | 51.1 |
| 6 | random | 10,000 | 173,749 | 51.2 |
| 6 | heuristic | 100 | 13,788 | 602.2 |
| 6 | heuristic | 1,000 | 17,771 | 605.1 |
| 6 | heuristic | 10,000 | 13,727 | 623.1 |

PufferLib overhead vs raw C: ~2.9x slower (history rewriting dominates remaining gap).
Optimizations applied: history reversal eliminated, `-O3 -march=native -flto`, incremental obs updates, absolute card encoding.
See [performance-puffer.md](performance-puffer.md) for remaining opportunities (#5 oldest-first history).

## C++ (OpenSpiel)

Uses OpenSpiel `State` API with explicit chance nodes (not auto-resolved).
Games take more "steps" than C engine (avg 21 vs 15 for 2p random).
Optimizations applied: `LegalActions()`/`ChanceOutcomes()` vector caching (avoids heap alloc per call), max turns tiebreaker.

| Players | Policy | Threads | Games/sec | Avg Game Length |
|---------|--------|---------|-----------|-----------------|
| 2 | random | 1 | 845,765 | 21.3 |
| 2 | heuristic | 1 | 49,201 | 604.0 |
| 6 | random | 1 | 260,838 | 67.7 |
| 6 | heuristic | 1 | 17,712 | 1,800.0 |
| 2 | random | 10 | 1,864,899 | 21.3 |

C++ vs raw C: ~3.5x slower single-threaded (2p random). Virtual dispatch + explicit chance nodes.
6p heuristic avg 1800 steps — C++ heuristic (action-priority, no card awareness) hits 200-turn cap more often than C heuristic.

## pyspiel (Python) — pending

Requires pyspiel import. Will be added when available.

## Methodology

- **C (raw)**: Direct `step_with_rng()` loop, no observation generation. Compiled with `-O3 -march=native -flto`.
- **PufferLib**: CPython extension. Each step generates full 439-float observation (407 engine + 32 action mask). C-side random/heuristic action selection simulates policy output.
- **C++ (OpenSpiel)**: OpenSpiel `State` API with explicit chance node handling. More steps per game than C (chance nodes are separate actions).
- **pyspiel**: Python game loop via `pyspiel.load_game()`. Python/SWIG overhead. Multi-process for threading.

### Player Policies
- **random**: Uniform random selection from valid actions.
- **heuristic**: Deterministic medium-bot strategy. Honest play (never bluffs), never challenges, always blocks. Priority: coup (7+ coins) > assassinate > tax (if Duke) > exchange (if Ambassador) > steal (if Captain) > income > foreign aid.

### Notes
- Games/sec = *completed games*, not individual steps.
- Avg game length = player decisions per game (chance nodes auto-resolved in C engine).
- Max turns = 200 main actions before tiebreak (prevents infinite heuristic deadlocks).
- Thread scaling uses independent game instances per thread (embarrassingly parallel).
- Heuristic games are 7-40x longer than random due to blocking/passing phases.
