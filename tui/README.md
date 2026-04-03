# TUI (Terminal UI)

Terminal-based Coup interface built with `@opentui/core`. Compiles the C engine into a shared library and uses it via Bun's FFI.

## Run

From the repo root:

```bash
bun run play
```

Or with options:

```bash
bun run play -- --players 4 --difficulty hard --fast
```

See the root README for all CLI flags.

## Key Files

```
src/
  index.ts          Entry point + arg parsing
  game-loop.ts      Main game loop
  setup.ts          Interactive setup screen
  layout.ts         Terminal layout
  input.ts          Key handling
  renderer/         Screen rendering
  animation/        Bot thinking animations
```

## Dependencies

Uses `@coup/game-client` (workspace package in `../packages/game-client/`) for the C engine FFI wrapper and bot agents.
