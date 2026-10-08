# PufferLib Performance Notes (historical)

> These notes describe the removed PufferLib 3.0-era CPython binding
> (`pufferlib/binding.c`, 407-float observation). The current env is the
> PufferLib 5.0 header `puffer/coup.h` (uint8 `coup_obs.h` observation, every
> seat an agent). It runs in PufferLib's own C/OpenMP vector loop, so the
> multiprocessing notes below no longer apply. Current numbers: `make bench-puffer`
> and `results.md`.

## Current Numbers (Mac Mini M4, single worker)

After all optimizations applied (history reversal removed, `-O3 -flto`, incremental obs):

| Players | Policy | Games/sec | Steps/sec | Avg Length |
|---------|--------|-----------|-----------|------------|
| 2 | random | 928,913 | 14,305,220 | 15.4 |
| 6 | random | 205,426 | 10,497,268 | 51.1 |
| 2 | heuristic | 131,999 | 14,042,693 | 106.3 |
| 6 | heuristic | 15,542 | 9,412,985 | 605.1 |

Original baseline was 780K / 174K / 112K / 14K. Total improvement: +19% / +18% / +18% / +11%.

Steps/sec is ~10-14M regardless of game length — per-step observation cost still dominates.
Raw C engine does ~48M steps/sec. The ~3.3x gap is mostly history rewriting (256 floats/step).

## How PufferLib Parallelizes

PufferLib's `vector.Multiprocessing` already handles multi-core scaling:
- Spawns N worker **processes** (default: 1 per hardware core, enforced)
- Each worker runs its own `vec_step()` on its own slice of envs
- Shared memory (`RawArray`) for observations, actions, rewards, terminals
- Semaphore-based synchronization between main process and workers
- Zero-copy when batch boundaries align with worker boundaries

**This means:** The C extension's `vec_step` loop should stay **single-threaded**. Adding OpenMP/pthreads inside it would oversubscribe cores (N processes × M threads). Parallelism is the framework's job, not the engine's.

## The Bottleneck: `write_obs()`

Called every step for every env. Does:
1. **History reversal** — O(n) ring buffer copy, up to 64 entries (lines 60-65 of binding.c)
2. **`observe()`** — memsets 407 floats to 0, then writes one-hot encodings, coin normalization, phase/player/action one-hots, and 64×4 history floats
3. **Action mask** — 32 float writes from a bitmask (cheap)

Most of the 407 floats don't change between steps. Only active player, pending action, responded mask, and the latest history entry change.

## Optimizations Applied

### 1. `-O3 -march=native -flto` for extension build -- DONE (+8%)

Changed `setup.py` from `-O2` to `-O3 -march=native -flto`.

### 2. Eliminate history reversal -- DONE (+7%)

Changed `observe()` to read the ring buffer directly via `history_get()` (newest-first convention). Removed the O(n) reversal loop in `write_obs()` in binding.c.

### 3. Incremental observation updates -- DONE (+10%)

Added `observe_incremental()` to `coup_core.c` with `ObsSnapshot` struct. Diffs game state fields against a snapshot, only rewrites changed floats.

Fuzz-tested: 500 games, 75,949 steps — exact float equality with full `observe()` on every step.

**Why the gain was moderate (+10%):** History still rewrites fully every step (256 floats) because newest-first encoding shifts all slots on each new entry.

### 4. Absolute card encoding -- DONE (+12%)

Changed observation from player-relative to absolute encoding. All 6 players' cards are at fixed positions [0-71] (player 0 always at [0-11], player 1 at [12-23], etc.). Card types only visible if (a) observer's own card, or (b) card is dead/revealed. Alive flag always public.

This eliminated the full-recompute fallback on player_id change in `observe_incremental()`. Now when the active player changes (every step), only the old self's and new self's alive card type one-hots toggle (~4-8 writes instead of 72).

Updated in both C and C++ engines. Fuzz-tested:
- 75,949 steps of incremental vs full `observe()` — exact float match
- 299,097 card visibility checks — no hidden card types leak to non-self observers

## Remaining Opportunities

### 5. Oldest-first history encoding (2 hr, est. +30-50%)

The single biggest remaining win. Change history in the observation tensor from newest-first to oldest-first:
- Current: slot 0 = most recent → every new entry shifts all 256 floats
- Proposed: slot 0 = oldest → new entry writes 4 floats at `write_ptr` position

This makes history truly append-only: 4 float writes per step instead of 256. That's a 64x reduction for the largest section of the observation (256 out of 407 floats).

**Architecture consideration:** Oldest-first is fine for transformers and LSTMs (process sequences naturally). For FFNs, it means the same event type could appear at any slot position, requiring the net to learn the pattern 64 times. If training with FFN, keep newest-first and accept the 256-float rewrite cost. If training with transformer/LSTM, oldest-first is a free win.

**Requirements:**
- Change `observe()` to iterate history oldest-first
- Change `observe_incremental()` to track a write pointer and only write the new entry
- Must update C++ engine `ObservationTensor` for cross-engine parity

### 6. Combined impact estimate (with oldest-first history)

With all optimizations applied + oldest-first history:
- Cards: ~4-8 writes (only on player_id change or reveal/loss)
- Coins: 1-2 writes
- Phase/active/turn/pending one-hots: 4-8 writes  
- Responded mask: 0-6 writes
- History: 4 writes (append-only)
- Total: ~15-25 writes per step (vs original ~407)

Expected: bring PufferLib from ~1.04M to ~1.5-2M games/sec for 2p random.

## What NOT to Do

- **Don't add threads to `vec_step()`** — PufferLib handles parallelism via multiprocessing (1 process per core). Threading inside would oversubscribe.
- **Don't skip observation generation** — the training loop reads obs immediately after `recv()` for the neural net forward pass. Every step needs valid obs.
- **Don't cache observations across resets** — env state is completely different after reset. Full `observe()` required.

## Priority

| # | Change | Effort | Impact | Status |
|---|--------|--------|--------|--------|
| 1 | `-O3 -flto` compiler flags | 5 min | +8% | DONE |
| 2 | Eliminate history reversal | 1 hr | +7% | DONE |
| 3 | Incremental observation updates | 4 hr | +10% | DONE (limited by history rewrite + card rotation) |
| 4 | Absolute card encoding | 3 hr | +12% | DONE |
| 5 | Oldest-first history encoding | 2 hr | +30-50% (est.) | Next — biggest remaining win (transformer/LSTM only) |
| 6 | SIMD observation writes | 4 hr | +10-15% | Later — diminishing returns after #5 |

**Note:** #5 changes observation layout semantics. No neural net is trained yet, so the convention is free to change. Requires updating C++ engine's `ObservationTensor` for cross-engine parity. Only beneficial for transformer/LSTM architectures — FFN should keep newest-first.
