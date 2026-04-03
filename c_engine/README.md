# C Engine

Pure C implementation of Coup. Bit-packed game state (~32 bytes), designed for high-throughput RL training via PufferLib.

## Build

```bash
# Shared library (for TUI/WASM)
cc -shared -O3 -o libcoup.dylib coup_core.c text_render.c

# Tests
make test-c    # from repo root
```

## Key Files

- `coup_core.h` -- Game struct, enums, 32-action space defines
- `coup_core.c` -- Game logic: `step()`, `observe()`, `get_valid_actions()`, chance nodes (~45KB)
- `text_render.c` -- Human-readable game state rendering
- `prng.h` -- xoshiro256** PRNG (header-only)
- `history.h` -- Ring buffer for PufferLib observation history (header-only)
- `heuristic.h` -- Heuristic bot logic (header-only)

## Design

- Always 6 player slots; unused slots are pre-killed
- One `step()` call = one decision by one player
- Stochasticity factored: `chance_outcomes()` for CFR enumeration, `step_with_rng()` for sampling
- Action masking via 32-bit bitmask from `get_valid_actions()`

See `plans/plan.md` for the full game rules spec, action indices, and observation layout.
