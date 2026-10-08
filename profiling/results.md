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

## PufferLib 5.0 env (`puffer/coup.h`)

`make bench-puffer`, Apple M4, single thread, 256 envs. Each step writes the
587-byte uint8 observation and 32-byte action mask for every seat; the active
seat picks a random legal action (included in the timing).

| Players | Agents | Env-steps/sec | Agent-steps/sec | Games/sec |
|---------|--------|---------------|-----------------|-----------|
| 2 | all | 4.54M | 9.07M | 295K |
| 4 | all | 2.40M | 9.61M | 71K |
| 6 | all | 1.46M | 8.75M | 29K |
| mixed 2-6 | all | 2.17M | 8.70M | 77K |
| 4 | 1 + 3 heuristic bots | 1.99M | 1.99M | 231K |

The numbers in older revisions of this file were for the removed 3.0-era
CPython binding (`pufferlib/binding.c`), which used a different observation and
had correctness bugs. They are not comparable.

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

## Methodology

- **C (raw)**: Direct `step_with_rng()` loop, no observation generation. Compiled with `-O3 -march=native -flto`.
- **PufferLib**: 5.0 env header driven by a fake Agent buffer harness (`puffer/test_coup_env.c`). Every seat's uint8 observation + action mask written every step.
- **C++ (OpenSpiel)**: OpenSpiel `State` API with explicit chance node handling. More steps per game than C (chance nodes are separate actions).

### Player Policies
- **random**: Uniform random selection from valid actions.
- **heuristic**: Deterministic medium-bot strategy. Honest play (never bluffs), never challenges, always blocks. Priority: coup (7+ coins) > assassinate > tax (if Duke) > exchange (if Ambassador) > steal (if Captain) > income > foreign aid.

### Notes
- Games/sec = *completed games*, not individual steps.
- Avg game length = player decisions per game (chance nodes auto-resolved in C engine).
- Max turns = 200 main actions before tiebreak (prevents infinite heuristic deadlocks).
- Thread scaling uses independent game instances per thread (embarrassingly parallel).
- Heuristic games are 7-40x longer than random due to blocking/passing phases.
