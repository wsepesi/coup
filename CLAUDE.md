# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Coup (the card game) implemented as a multi-agent RL environment with two independent game engines and multiple frontends. The C engine targets PufferLib (PPO training), the C++ engine targets OpenSpiel (CFR/game-theoretic analysis), and a TypeScript layer provides a playable TUI and game client.

## Build & Test Commands

```bash
# Run all tests (C engine + cross-framework)
make test

# Individual test targets
make test-c          # C engine core + PRNG tests
make test-text       # Text renderer tests
make test-cpp        # C++ engine via CMake (requires OpenSpiel submodule)
make test-cross      # Cross-framework Python tests (uv run)
make build-puffer    # Build PufferLib C extension (uv run python setup.py build_ext --inplace)
make test-puffer     # PufferLib perf benchmark

# TUI (Bun)
bun run play         # Build libcoup shared lib + launch TUI
bun run build        # Just build libcoup (tui/build.sh)
bun test tui/tests/  # TUI tests

# Clean
make clean
```

Python commands use `uv run`. Bun is the JS runtime. The TUI compiles `c_engine/coup_core.c` into a shared library (`libcoup.dylib`/`.so`) for FFI access.

## Architecture

### Dual-Engine Design

Two fully independent implementations of the same game, connected only by shared rules (defined in `plans/plan.md`) and cross-framework tests:

- **`c_engine/`** — Pure C. Bit-packed game state (~32 bytes). Used by PufferLib for high-throughput RL training. Key files: `coup_core.h` (structs, enums, action defines), `coup_core.c` (game logic + observation), `prng.h` (xoshiro256**), `history.h` (ring buffer for PufferLib wrapper).

- **`cpp_engine/`** — Pure C++. OpenSpiel `State`/`Game` subclasses. Used for CFR and game-theoretic analysis. Builds via CMake against `lib/OpenSpiel/` submodule.

Both engines share the same **fixed 32-action space** with stable index semantics: 0=income, 1=foreign_aid, 2=tax, 3=exchange, 4-9=coup targets, 10-15=steal targets, 16-21=assassinate targets, 22=challenge, 23=pass, 24-27=block variants, 28-31=discard slots.

### TypeScript Layer

- **`packages/game-client/`** — Core game wrapper over C engine FFI (`bun:ffi` → `libcoup`). Contains `CoupGame` class, type definitions, and AI agents (heuristic easy/medium/hard, ONNX stub). This is a Bun workspace package.

- **`tui/`** — Terminal UI using `@opentui/core`. Renders the game with the CliRenderer. `tui/build.sh` compiles the shared library before launching.

### Training

- **`pufferlib/`** — PufferLib integration. `binding.c` is a CPython extension wrapping the C engine. `coup_env.py` is the PufferEnv subclass. Single-agent framing with player rotation.

- **`training/puffer_hello.py`** — Minimal PPO self-play script (hello-world, not tuned).
- **`training/openspiel_hello.py`** — Minimal MCCFR script.

### Key Design Decisions

- Game struct is always 6 player slots; unused slots are pre-killed (dead cards, 0 coins).
- One `step()` call = one decision by one player. Chance nodes are separate steps.
- Stochasticity is factored: `chance_outcomes()` for CFR enumeration, `step_with_rng()` for PufferLib sampling.
- Action masking: valid actions returned as a bitmask via `get_valid_actions()`. In observations, last 32 floats are the mask.
- `plans/plan.md` is the **source of truth** for game rules, action indices, phase state machine, and observation layout.

## Cross-Framework Tests

The `tests/` directory enforces parity between engines:
- `test_prng_identity.py` — bit-identical PRNG output between C and C++
- `shared/test_constants.py` — enum/action values match across engines
- `tests/test_cross_determinism.py` — same action sequences produce same outcomes (requires both engines built)
- `tests/test_observation_parity.py` — observation tensors match for identical states (requires both engines built)
