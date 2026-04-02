# Coup: Development Plan

## Overview

Two separate game engines (pure C for PufferLib, pure C++ for OpenSpiel) implementing Coup for 2-6 players, with framework integrations and training scaffolding. No downstream clients (web, TUI, LLM interface) in this phase.

Designed for execution by 5-10 concurrent subagents. Each task has a status field that agents use as a faux-lock: set to `CLAIMED: <agent-id>` before starting, `DONE` when complete. Do not start a task that is already claimed. Respect dependency chains.

---

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Game struct | Always 6 slots, mask unused players dead | One struct size, one observation shape, simpler everywhere |
| C vs C++ engines | Fully separate implementations | Path of least resistance for both frameworks; cross-fuzz tests enforce parity |
| Constants sync | By convention + tests, no shared header | Keeps engines fully independent; test catches drift |
| OpenSpiel integration | Git submodule, dynamic `RegisterGame()`, standalone CMake | No modification of OpenSpiel source tree |
| PufferLib integration | Out-of-tree, copy `env_binding.h`, own `setup.py` | Clean separation, follows current PufferLib pure-C pattern |
| PufferLib ring buffer | Owned by PufferLib wrapper, not core Game struct | OpenSpiel uses its own `State::history_`; buffer is a PPO training concern |
| OpenSpiel RNG | None — all stochasticity via `ChanceOutcomes()` / `DoApplyAction()` | Required by CFR; matching rollouts via recorded chance outcome replay |
| Self-play | Single shared policy, player rotation, v1 | Simplest; observation encodes seat identity |
| Rewards | +1 winner / -1 losers terminal; intermediate reward hooks present but zeroed | Keeps options open for shaping and placement-based rewards downstream |
| Player count | 2-6, configured at init, struct always 6 slots | `num_players` field; unused slots pre-killed |
| Training | Hello-world scaffolding only — working env + skeleton scripts | Actual training config/tuning is a later phase |
| Build | Top-level Makefile orchestrating C tests, CMake (OpenSpiel), setuptools (PufferLib) | Single entry point for all builds |

---

## Repository Structure (Target)

```
coup/
├── Makefile                     # Top-level: delegates to sub-builds
├── pyproject.toml               # uv-managed, setuptools build for PufferLib extension
├── setup.py                     # Build C extension for PufferLib binding
├── dev-plan.md                  # This file (task tracking)
├── plans/plan.md                # Design spec (source of truth for game rules + encoding)
│
├── shared/                      # Shared test utilities only (no shared engine code)
│   └── test_constants.py        # Asserts C and C++ enum/action values match
│
├── c_engine/                    # Pure C engine (PufferLib target)
│   ├── coup_core.h              # Game struct, enums, function declarations
│   ├── coup_core.c              # Game logic: step, observe, mask, chance
│   ├── prng.h                   # xoshiro256** (header-only)
│   ├── history.h                # Ring buffer struct + helpers (header-only)
│   └── test_core.c              # Standalone C tests (assert + exit)
│
├── pufferlib/                   # PufferLib integration (out-of-tree)
│   ├── env_binding.h            # Copied from PufferLib (CPython glue framework)
│   ├── binding.c                # CPython extension: init, step, log
│   ├── coup_env.py              # PufferEnv subclass
│   └── test_perf.py             # SPS benchmark (random actions)
│
├── cpp_engine/                  # Pure C++ engine (OpenSpiel target)
│   ├── coup_game.h              # CoupGame + CoupState class declarations
│   ├── coup_game.cc             # Implementation
│   ├── coup_game_test.cc        # OpenSpiel-style tests (RandomSimTest etc.)
│   └── CMakeLists.txt           # Builds against OpenSpiel submodule
│
├── lib/
│   ├── PufferLib/               # Git submodule
│   └── OpenSpiel/               # Git submodule
│
├── tests/
│   ├── test_cross_determinism.py  # Same action sequence through both engines, compare outcomes
│   ├── test_observation_parity.py # Same game state, compare observation tensors
│   └── test_prng_identity.py      # C xoshiro256** vs C++ implementation, bit-identical
│
└── training/
    ├── puffer_hello.py           # Minimal PPO self-play script (env loads, steps, learns)
    └── openspiel_hello.py        # Minimal MCCFR / exploitability script
```

---

## Task Dependency Graph

```
                    T0 (PRNG)
                   /         \
                 T1            T2
            (C engine)    (C++ engine)
            /    |    \        |    \
          T3    T4    T5      T6    T7
     (PufferLib) (C   (hist)  (OS   (OS
      binding)  tests) ring)  integ) tests)
          |                    |
          T8                  T9
     (PufferLib           (OS build
      env.py)              + register)
          |                    |
          T10                 T11
     (perf test)          (OS test suite)
          \                   /
           \                 /
            T12  T13  T14  T15
           (cross-det) (obs-parity) (const-sync) (PRNG-identity)
                    \     |     /
                     \    |    /
                      T16  T17
                 (puffer hello) (openspiel hello)
                        |
                       T18
                    (top-level Makefile)
```

---

## Tasks

### Phase 0: Foundation

#### T0 — PRNG Implementation
- **Status:** `DONE`
- **Deps:** none
- **Output:** `c_engine/prng.h`
- **Spec:**
  - xoshiro256** with SplitMix64 seeding, header-only, `extern "C"` compatible
  - `xoshiro256_seed(state, seed)`, `xoshiro256_next(state)`, `xoshiro256_uniform(state, n)`
  - Rejection sampling for uniform (no modulo bias)
  - Standalone test: init with seed 42, generate 10,000 values, print first 10 + hash of all for manual C++ comparison
  - This file is used by the C engine only; the C++ engine will have its own implementation for the PRNG identity test, but OpenSpiel's engine doesn't use RNG at runtime

---

### Phase 1: Core Engines (parallel)

#### T1 — C Engine: Game Logic
- **Status:** `DONE`
- **Deps:** T0
- **Output:** `c_engine/coup_core.h`, `c_engine/coup_core.c`
- **Spec:**
  - `Game` struct: 32 bytes as specified in plan.md section 3 (6 × uint16_t players, uint16_t deck, phase_state, aux, aux2, pad, uint64_t rng_state)
  - `num_players` field (2-6), unused player slots pre-killed (both cards revealed, coins=0, bits set in responded_mask)
  - `game_init(Game*, num_players, deal_seed, proc_seed)` — deals cards via PRNG, sets initial state
  - `step_deterministic(Game*, action)` — applies one player decision, no RNG consumed
  - `step_with_rng(Game*, action)` — calls step_deterministic, then resolves all pending chance nodes via internal PRNG
  - `get_valid_actions(Game*) -> uint32_t` — bitmask over 32-action space
  - `is_chance_node(Game*) -> bool`
  - `chance_outcomes(Game*, ChanceOutcome* out) -> int` — returns (outcome, probability) pairs
  - `apply_chance(Game*, outcome)` — deterministic application of one chance result
  - `get_active_player(Game*) -> int`
  - `is_done(Game*) -> bool`
  - `get_winner(Game*) -> int` (or -1 if not done)
  - Full phase state machine per plan.md section 5 (DEAL, CHANCE_REDRAW, CHANCE_EXCHANGE, MAIN_ACTION, CHALLENGE_ACTION, BLOCK, CHALLENGE_BLOCK, LOSE_CARD, EXCHANGE_DISCARD, RESOLVE)
  - Cycling logic for challenges/blocks: iterate from player after turn_player, skip dead, use responded_mask
  - 10+ coins forced coup (mask constraint only)
  - Foreign Aid blockable by any living non-acting player claiming Duke
  - Assassination coin deduction on declaration (before challenge/block resolution)
  - Challenge defense: reveal card, shuffle back into deck, draw replacement (chance node)
  - Ambassador exchange: draw 2 (chance nodes), then 2 sequential discard decisions with canonical ordering
  - All action index semantics per plan.md section 4 (0=income through 31=discard_slot_3)
  - Enums for Phase, CardType, action indices as #defines or enum

#### T2 — C++ Engine: Game Logic
- **Status:** `DONE`
- **Deps:** none (uses plan.md as spec, not C code)
- **Output:** `cpp_engine/coup_game.h`, `cpp_engine/coup_game.cc`
- **Spec:**
  - OpenSpiel `State` and `Game` subclasses
  - `CoupGame` implements: `NumDistinctActions()` (32), `NewInitialState()`, `NumPlayers()`, `MinUtility()` (-1), `MaxUtility()` (1), `MaxGameLength()` (conservative upper bound), game type with `kSequential`, `kExplicitStochastic`, `kImperfectInformation`
  - `CoupState` implements: `CurrentPlayer()` (returns `kChancePlayerId` during deal/redraw/exchange_draw, 0-N for player decisions, `kTerminalPlayerId` when done), `LegalActions()`, `DoApplyAction()`, `ChanceOutcomes()`, `IsTerminal()`, `Returns()`, `Clone()`, `InformationStateString()`, `InformationStateTensor()`, `ToString()`
  - Internal state representation can differ from C engine's bit-packing — clarity over compression is fine for C++
  - Same game rules, same action index semantics (0-31 as in plan.md section 4), same phase state machine
  - No RNG — all stochasticity expressed through chance nodes
  - `num_players` parameter (2-6), same unused-slot-pre-killed convention
  - `Clone()` via copy constructor (state is a value type, no heap)
  - `InformationStateString(player)`: canonical string encoding of player's private info + all public info + full action history (OpenSpiel's `State::history_` provides the action history)
  - `InformationStateTensor(player)`: ~410 floats matching the observation layout in plan.md section 6 (own cards one-hot, other players' public info, coins, phase, etc.) — must match what the C engine's observe() produces for identical game states
  - `Utility`: `kZeroSum` for 2-player, `kGeneralSum` for 3+ (or just use `kGeneralSum` always since +1/-1 is compatible)

---

### Phase 2: Engine Support (parallel, after respective Phase 1 dep)

#### T3 — C Engine: Observation Function
- **Status:** `DONE`
- **Deps:** T1
- **Output:** additions to `c_engine/coup_core.h` / `c_engine/coup_core.c`
- **Spec:**
  - `observe(Game*, player_id, HistoryEntry* history, int history_len, float* out)` — writes ~410 floats
  - Layout per plan.md section 6: own cards (one-hot + alive), other players (revealed cards, alive), coins, alive_mask, phase, active_player, turn_player, pending_action, responded_mask, exchange_cards, history entries
  - Pure function, no side effects, no allocation
  - Player's own hidden cards visible; other players' hidden cards NOT visible
  - Exchange cards visible only to the exchanging player during EXCHANGE_DISCARD phase
  - History entries encoded as 4 floats each (acting_player normalized, action normalized, phase normalized, result flags)
  - Empty history slots are zeros
  - Also: `get_observation_size() -> int` (returns the fixed obs dimension)

#### T4 — C Engine: Tests
- **Status:** `DONE`
- **Deps:** T1
- **Output:** `c_engine/test_core.c`
- **Spec:**
  - Standalone executable, asserts + exit(1) on failure
  - Tests:
    1. Init with known seed, verify card deal is deterministic
    2. Full game rollout with "always pick first legal action" policy, verify terminal state
    3. Mask correctness: 10+ coins only allows coup targets; dead players never targetable; phase-appropriate actions only
    4. Challenge resolution: both success and failure paths, verify card swap + deck update
    5. Block resolution: honest block, bluff block challenged, block unchallenged
    6. Ambassador exchange: draw 2, discard 2 with canonical ordering, verify deck/hand consistency
    7. Player elimination: verify dead player skipped in all cycling
    8. 2-player game completes correctly
    9. 6-player game completes correctly
    10. Foreign aid block cycling: all non-acting players get chance to block
    11. Chance node enumeration: verify probabilities sum to 1.0, outcomes cover all non-zero deck types

#### T5 — C Engine: History Ring Buffer
- **Status:** `DONE`
- **Deps:** T1
- **Output:** `c_engine/history.h`
- **Spec:**
  - `HistoryEntry` struct: 4 bytes (acting_player, action, phase, result)
  - `HistoryBuffer` struct: `HistoryEntry entries[64]`, `uint8_t head`, `uint8_t len`
  - `history_push(buf, entry)` — circular write
  - `history_get(buf, index) -> HistoryEntry` — 0 = most recent
  - Header-only, no allocation
  - This is used by the PufferLib wrapper, not the core engine

#### T6 — C++ Engine: OpenSpiel Integration Scaffolding
- **Status:** `DONE`
- **Deps:** T2
- **Output:** `cpp_engine/CMakeLists.txt` (updated), registration code in `coup_game.cc`
- **Spec:**
  - `CMakeLists.txt` that:
    - Adds `lib/OpenSpiel/` via `add_subdirectory()` or `find_package()`
    - Builds `coup_game.cc` as a shared library or object that links against OpenSpiel
    - Creates `coup_game_test` executable
  - `REGISTER_SPIEL_GAME(kGameType, Factory)` in `coup_game.cc` for dynamic registration
  - Game parameters: `{"players", GameParameter(2)}` (default 2, range 2-6)
  - Verify: `pyspiel.load_game("coup(players=2)")` works from Python after build

#### T7 — C++ Engine: Tests
- **Status:** `DONE`
- **Deps:** T2, T6
- **Output:** `cpp_engine/coup_game_test.cc`
- **Spec:**
  - Uses OpenSpiel test utilities from `open_spiel/tests/basic_tests.h`
  - Tests:
    1. `testing::LoadGameTest("coup")` — game loads from registry
    2. `testing::RandomSimTest(*game, num_sims=1000)` — random playouts without crashing
    3. `testing::ChanceOutcomesTest(*game)` — chance probabilities valid
    4. Custom: verify 2-player and 6-player games reach terminal
    5. Custom: verify InformationStateString differs for players with different hands
    6. Custom: verify InformationStateTensor shape matches expected obs dimension
    7. Custom: verify action semantics (index 0 = income, 22 = challenge, etc.)

---

### Phase 3: Framework Integration (parallel, after Phase 2)

#### T8 — PufferLib: Binding + Env
- **Status:** `DONE`
- **Deps:** T1, T3, T5
- **Output:** `pufferlib/env_binding.h`, `pufferlib/binding.c`, `pufferlib/coup_env.py`
- **Spec:**
  - Copy `env_binding.h` from PufferLib's `ocean/` directory
  - `binding.c`:
    - `#include "../c_engine/coup_core.h"` and `"../c_engine/history.h"`
    - Define `CoupEnv` struct containing: `Game game`, `HistoryBuffer history`, plus required PufferLib fields (`float* observations`, `int* actions`, `float* rewards`, `uint8_t* terminals`, `Log log`)
    - `c_reset(CoupEnv*)`: call `game_init()`, clear history, write initial observation
    - `c_step(CoupEnv*)`: read action from buffer, call `step_with_rng()`, push to history, write observation, check terminal, write reward (+1/-1 at game end, 0 otherwise)
    - **Single-agent framing with player rotation**: each step is one decision by the active player. After step, auto-resolve chance nodes and advance to next human decision point. The observation always represents the current active player's view.
    - `Log` struct: fields for `episode_length`, `winner_seat`, `game_result` (1.0 for win), `n`
    - `c_render()`: no-op
    - `c_close()`: no-op
    - `init()`: unpack kwargs for `num_players`, `seed`
    - `log()`: populate dict from Log fields
  - `coup_env.py`:
    - Subclass `pufferlib.PufferEnv`
    - `single_observation_space`: Box(0, 1, shape=(OBS_DIM,), float32)
    - `single_action_space`: Discrete(32)
    - Action masking: last 32 floats of observation are the mask (convention from plan.md). Policy applies mask in `decode_actions()`.
    - `num_agents = num_envs` (one agent per env instance, single-agent framing)
  - `setup.py` (or extension in `pyproject.toml`): build `binding.c` as CPython extension, include paths for `c_engine/` and numpy

#### T9 — OpenSpiel: Build + Registration
- **Status:** `DONE`
- **Deps:** T2, T6
- **Output:** working CMake build, `pyspiel.load_game("coup")` succeeds
- **Spec:**
  - Finalize CMakeLists.txt so it builds cleanly against the OpenSpiel submodule
  - Verify dynamic registration works: a test binary that loads the game from the registry
  - Verify Python bindings work: `import pyspiel; game = pyspiel.load_game("coup(players=2)")`
  - Document any environment setup (LD_LIBRARY_PATH, PYTHONPATH) in comments or a build note

---

### Phase 4: Testing + Validation (parallel, after Phase 3)

#### T10 — PufferLib: Performance Test
- **Status:** `DONE`
- **Deps:** T8
- **Output:** `pufferlib/test_perf.py`
- **Spec:**
  - Create env with 1000 parallel instances
  - Run random actions for 10 seconds
  - Report steps/second
  - Target: >1M SPS (game logic is ~20 instructions per step)
  - Also serves as a smoke test — if it runs 10M steps without crashing, the env is solid

#### T11 — OpenSpiel: Test Suite
- **Status:** `DONE`
- **Deps:** T7, T9
- **Output:** passing `coup_game_test` executable
- **Spec:**
  - All tests from T7 pass
  - Run via `ctest` or directly
  - Also run from Python: `pyspiel.load_game("coup(players=2)")`, create state, run random playout

#### T12 — Cross-Framework Determinism Test
- **Status:** `DONE`
- **Deps:** T8, T9
- **Output:** `tests/test_cross_determinism.py`
- **Spec:**
  - For seeds [42, 123, 999, 0, 2^32-1]:
    - Run C engine with "always first legal action" policy, record: (active_player, action) sequence + chance outcomes + final winner
    - Run C++ engine (via pyspiel) with same action sequence, feeding recorded chance outcomes via `DoApplyAction()` at chance nodes
    - Assert: identical (player, action) sequences at player decision nodes, identical terminal returns
  - For seeds [42, 123]: run with 2, 3, 4, 5, 6 players each
  - This test is the contract that both engines implement the same game

#### T13 — Observation Parity Test
- **Status:** `DONE`
- **Deps:** T3, T9
- **Output:** `tests/test_observation_parity.py`
- **Spec:**
  - Play a game in both engines simultaneously (same action/chance sequence as T12)
  - At each player decision node, for each player:
    - Get C engine observation via `observe()`
    - Get C++ engine observation via `InformationStateTensor()`
    - Assert: vectors are identical (or within float epsilon)
  - This ensures neural networks trained in one framework can evaluate in the other

#### T14 — Constants Sync Test
- **Status:** `DONE`
- **Deps:** T1, T2
- **Output:** `tests/test_constants_sync.py` (or `shared/test_constants.py`)
- **Spec:**
  - Extract constants from both engines (action indices, phase enums, card types, observation dimensions)
  - C engine: either parse header or expose via a small test binary that prints values
  - C++ engine: expose via pyspiel game properties or a small test binary
  - Assert all values match
  - This catches enum drift between independent implementations

#### T15 — PRNG Identity Test
- **Status:** `DONE`
- **Deps:** T0
- **Output:** `tests/test_prng_identity.py`
- **Spec:**
  - C implementation: small binary that seeds xoshiro256** with seed 42, outputs first 1000 values as hex
  - C++ implementation (standalone, not part of OpenSpiel engine since it doesn't use RNG): same algorithm, same seed, outputs first 1000 values
  - Assert: bit-identical output
  - Purpose: validates that if we ever need to replay C PRNG sequences in C++ (e.g., for debugging), the implementations match
  - Note: the C++ OpenSpiel engine does NOT use this PRNG at runtime — this test is for the cross-determinism replay infrastructure

---

### Phase 5: Training Scaffolding (after Phase 4)

#### T16 — PufferLib Hello World Training
- **Status:** `DONE`
- **Deps:** T10
- **Output:** `training/puffer_hello.py`
- **Spec:**
  - Minimal script that:
    1. Creates a CoupEnv with 128 parallel envs
    2. Creates a simple MLP policy (3-layer, 256 hidden) with action masking in decode_actions
    3. Runs PufferLib's PPO for 100k steps
    4. Prints mean episode length and win rate over last 1000 episodes
  - NOT tuned, NOT a real training run — just proves the pipeline works end-to-end
  - Includes a `Policy` class (PyTorch nn.Module) with:
    - `encode_observations(obs)` → hidden
    - `decode_actions(hidden, lookup)` → masked logits + value
    - The mask is extracted from the observation (last 32 floats)
  - Config: lr=2.5e-4, gamma=0.99, gae_lambda=0.95, clip_coef=0.2, ent_coef=0.01 (PufferLib defaults)
  - Should run on CPU or CUDA (detected automatically)
  - Comments in code noting where to add: LSTM/transformer, larger networks, reward shaping, population training

#### T17 — OpenSpiel Hello World
- **Status:** `DONE`
- **Deps:** T11
- **Output:** `training/openspiel_hello.py`
- **Spec:**
  - Minimal script that:
    1. Loads `coup(players=2)` via pyspiel
    2. Runs external sampling MCCFR for N iterations (start small, ~1000)
    3. Prints exploitability (if feasible at this game size — may need abstraction)
    4. Alternatively: runs a random playout and prints the game tree depth / info set count estimate
  - NOT a real training run — proves the OpenSpiel integration works for game-theoretic algorithms
  - Comments noting: where to add Deep CFR, abstraction, policy export for cross-framework eval

#### T18 — Top-Level Makefile
- **Status:** `DONE`
- **Deps:** T4, T6, T8 (needs all build targets defined)
- **Output:** `Makefile`
- **Spec:**
  - Targets:
    - `make test-c` — builds and runs `c_engine/test_core.c` standalone tests
    - `make test-cpp` — builds C++ engine + OpenSpiel tests via CMake, runs `coup_game_test`
    - `make build-puffer` — builds the PufferLib C extension (`python setup.py build_ext --inplace`)
    - `make test-puffer` — runs `pufferlib/test_perf.py`
    - `make test-cross` — runs all cross-framework tests (T12-T15)
    - `make test` — runs all of the above
    - `make clean` — removes build artifacts
  - Uses `uv run` for all Python invocations
  - Assumes `lib/PufferLib` and `lib/OpenSpiel` submodules are initialized

---

## Execution Notes for Subagents

### Claiming a task
1. Read this file
2. Find a `TODO` task whose dependencies are all `DONE`
3. Edit the status to `CLAIMED: <your-agent-id>`
4. Do the work
5. Edit the status to `DONE`
6. If blocked, set status back to `TODO` with a note

### General rules
- **plan.md is the source of truth** for game rules, action indices, phase semantics, state encoding, and observation layout. Read it before implementing.
- **Do not modify another agent's files** unless your task explicitly says to add to them (e.g., T3 adds to T1's files).
- **Tests are first-class deliverables**, not afterthoughts. A task is not done until its tests pass.
- **Keep implementations minimal.** Don't add features beyond the task spec. Don't add comments explaining obvious code. Don't add error handling for impossible states.
- **Action index semantics are fixed.** 0=income, 1=foreign_aid, 2=tax, 3=exchange, 4-9=coup targets, 10-15=steal targets, 16-21=assassinate targets, 22=challenge, 23=pass, 24-27=block variants, 28-31=discard slots. Both engines must use these exact indices.

### Parallelism map

```
Time →
─────────────────────────────────────────────────────────────

Agent 1:  [T0: PRNG] → [T1: C engine core] → [T3: observe] → [T8: PufferLib binding]
Agent 2:                [T2: C++ engine    ] → [T6: OS scaffolding] → [T9: OS build]
Agent 3:                                       [T4: C tests  ] → [T12: cross-det]
Agent 4:                                       [T5: history  ] → [T13: obs-parity]
Agent 5:                                       [T7: C++ tests] → [T11: OS test suite]
Agent 6:                                                         [T10: perf test]
Agent 7:                                                         [T14: const-sync]
Agent 8:                                                         [T15: PRNG identity]
Agent 9:                                                                → [T16: puffer hello]
Agent 10:                                                               → [T17: OS hello]
                                                                        → [T18: Makefile]
```

Peak concurrency: ~8 agents during Phase 2-3 overlap. Minimum sequential depth: T0 → T1 → T3 → T8 → T10 → T16 (6 steps on the critical path).
