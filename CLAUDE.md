# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Coup (the card game) implemented as a multi-agent RL environment with a single C rules engine and multiple frontends. The C engine is used directly by PufferLib (PPO training), wrapped by a thin C++ adapter for OpenSpiel (CFR/game-theoretic analysis), compiled to WASM for the web stack, and loaded over FFI by the TypeScript TUI/game client.

## Build & Test Commands

```bash
# Run all tests (C engine; test-cpp is separate since it builds OpenSpiel)
make test

# Individual test targets
make test-c          # C engine core + PRNG + search-API tests
make test-text       # Text renderer tests
make test-cpp        # OpenSpiel adapter tests via CMake (requires OpenSpiel submodule)
make mccfr-example   # Outcome-sampling MCCFR demo on 2p Coup (C++)
make pyspiel         # Build OpenSpiel's real pyspiel (with coup) from lib/OpenSpiel into cpp_engine/build-py/python + .venv .pth
make test-py         # Python smoke tests (training/tests): pyspiel API, MCCFR/ISMCTS, torch+jax DQN/NFSP/PG/PPO/R-NaD/PSRO
make puffer-install  # Symlink puffer/coup.h + c_engine sources + coup.ini into lib/PufferLib (idempotent)
make puffer-cpu      # Build PufferLib CPU eval binary lib/PufferLib/coup (needs bash>=4 + libomp on macOS)
make test-puffer     # PufferLib env invariant tests (puffer/test_coup_env.c)
make bench-puffer    # PufferLib env single-thread throughput

# TUI (Bun)
bun run play         # Build libcoup shared lib + launch TUI
bun run build        # Just build libcoup (tui/build.sh)
bun test tui/tests/  # TUI tests

# Clean
make clean
```

Python commands use `uv run`. Bun is the JS runtime. The TUI compiles `c_engine/coup_core.c` into a shared library (`libcoup.dylib`/`.so`) for FFI access.

## Architecture

### Single Engine, Multiple Adapters

There is exactly one implementation of the rules: the C engine. Everything else wraps it, so there is no cross-engine parity to maintain.

- **`c_engine/`** — Pure C. Bit-packed game state (56 bytes incl. RNG). Key files: `coup_core.h` (structs, enums, action defines), `coup_core.c` (game logic + legacy observation), `coup_obs.h` (egocentric uint8 RL observation + event tracker, shared by PufferLib and OpenSpiel), `prng.h` (xoshiro256**), `history.h` (ring buffer).
- **`c_engine/coup_search.{h,c}`** — Opt-in search API (separate TU + side structs; no Game layout or hot-path changes): `CoupLogEvent`/`CoupLog` history log (`coup_event_make`, `coup_step_logged[_rng]`, `coup_log_same_view`), `CoupPublicState` (+ exact pack / 64-bit key, public legal mask), canonical private-hand space (15/5/1 hands), deck derivation, `coup_game_from_public[_idx]` (public state + hidden assignment → Game), belief-consistency helpers, and `coup_resample[_game]` (sample a history consistent with one player's view). For ReBeL/PoG-style public-belief search and IS-MCTS without OpenSpiel; see `c_engine/README.md` (incl. the canonical slot-order caveats). Tests: `c_engine/test_search.c`.

- **`cpp_engine/`** — Thin OpenSpiel adapter (`coup_game.cc`) over the C engine (`extern "C"`; `coup_core.c` and `coup_search.c` are compiled as C in the same CMake project). `CoupState` = C `Game` + event log (`std::vector<CoupLogEvent>` recorded by `coup_event_make`, for perfect-recall, leak-free `InformationStateString`) + `CoupObsTracker` (for `ObservationTensor`). `InformationStateTensor` (860) = observation + whole-game per-seat claim/challenge counters; `ResampleFromInfostate` wraps the C `coup_resample` (token/constraint sampler, for IS-MCTS) and replays the result. Deal/redraw/exchange draws are explicit chance nodes (`game_init(g, n, 0, 0)` leaves the deal undealt; the adapter never calls `step_with_rng`). Returns are zero-sum: winner +1, others -1/(n-1). Game params: `players` (2-6), `refund_on_challenge`. Builds via CMake against the `lib/OpenSpiel/` submodule; not registered in pip `pyspiel`, so `make pyspiel` (CMake option `COUP_BUILD_PYSPIEL=ON`, separate dir `cpp_engine/build-py`) builds OpenSpiel's own pyspiel target with the adapter objects added.

All layers share the same **fixed 32-action space** with stable index semantics: 0=income, 1=foreign_aid, 2=tax, 3=exchange, 4-9=coup targets, 10-15=steal targets, 16-21=assassinate targets, 22=challenge, 23=pass, 24-27=block variants, 28-31=discard slots.

### TypeScript Layer

- **`packages/game-client/`** — Core game wrapper over C engine FFI (`bun:ffi` → `libcoup`). Contains `CoupGame` class, type definitions, and AI agents (heuristic easy/medium/hard, ONNX stub). This is a Bun workspace package.

- **`tui/`** — Terminal UI using `@opentui/core`. Renders the game with the CliRenderer. `tui/build.sh` compiles the shared library before launching.

### Training

- **`puffer/`** — PufferLib 5.0 env (submodule `lib/PufferLib`, branch `5.0`; 5.0 has no Python package — the trainer is the `./puffer` CLI built by `lib/PufferLib/build.sh coup`, CUDA required; `--cpu` builds a CPU eval binary). `coup.h` is a single-header ocean env that `#include`s `coup_core.c`, `heuristic.h`, `coup_obs.h`; `coup.ini` is its config; `install.sh` symlinks both into the submodule (git-excluded, idempotent). One Env = one table; every learning seat is an Agent every step, non-active seats get a PASS-only mask; relative (egocentric) actions; terminal reward +1 / -1/(n-1) to all seats on the same step; seats rotate per game; seats past `num_agents` are scripted bots (`bot_policy`); `my_vec_init` packs mixed table sizes into buffers. Don't edit files inside `lib/PufferLib/`. See `puffer/README.md` (DGX steps).
- **`cpp_engine/mccfr_example.cc`** — Outcome-sampling MCCFR demo (`make mccfr-example`). `training/openspiel_hello.py` runs a pyspiel playout if `make pyspiel` was done, else launches it.
- **Python / OpenSpiel algorithms** — root `pyproject.toml` (uv, Python 3.12, `package = false`; extras `torch`, `jax`, `misc` (cvxpy/matplotlib), `jax-cuda`). pyspiel is not a pip dep: `make pyspiel` builds it and writes `.venv/.../coup_openspiel.pth` (lib/OpenSpiel + cpp_engine/build-py/python); re-run it after recreating `.venv`. Smoke tests in `training/tests/`. R-NaD was removed upstream, so `training/rnad/rnad.py` is a vendored copy. External-sampling traversals (ES-MCCFR, Deep CFR, ESCHER) and full-tree methods (tabular CFR, exact exploitability/BR) are infeasible on full Coup.

### Key Design Decisions

- Game struct is always 6 player slots; unused slots are pre-killed (dead cards, 0 coins).
- One `step()` call = one decision by one player. Chance nodes are separate steps.
- Stochasticity is factored: `chance_outcomes()` for CFR enumeration, `step_with_rng()` for PufferLib sampling.
- Action masking: valid actions returned as a bitmask via `get_valid_actions()` (absolute seats) or `coup_valid_actions_rel()` (egocentric, used by RL). The PufferLib env passes it as the native uint8 action mask; RL observations (`coup_obs_write`, `COUP_OBS_SIZE` uint8) do not include it. `OBS_SIZE`/`observe()` in `coup_core.h` are the legacy 407-float observation.
- `c_engine/coup_core.h` (bit layout, phase state machine, action indices, step/chance contracts) and `c_engine/coup_obs.h` (RL observation layout) are the **source of truth**. Rules follow the 2012 rulebook incl. the 2-player rule (starting player gets 1 coin); house rules: `refund_on_challenge` flag, sequential response order, MAX_TURNS=200 tiebreak.

## OpenSpiel Adapter Tests

`cpp_engine/coup_game_test.cc` (`make test-cpp`) runs OpenSpiel's `RandomSimTest` for 2-6 players, an infostate no-leak test (states differing only in an opponent's hidden cards/exchange picks give identical infostates and observations), public-revelation checks, zero-sum returns incl. the MAX_TURNS tiebreak, an exhaustive depth-limited CFR-style traversal (legal actions consistent per infoset; observations are a function of the infostate), undo/clone, an outcome-sampling MCCFR smoke run, info-state tensor checks, and `ResampleFromInfostate` checks (OpenSpiel's `ResampleInfostateTest` for 2-6 players, play-on from resampled states, post-deal distribution, constraint respect).
