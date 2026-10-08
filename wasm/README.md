# WASM

Compiles the C game engine to a standalone WebAssembly module for the Workers game server.

## Build

Requires [Emscripten](https://emscripten.org/):

```bash
./build.sh
```

Outputs `coup.wasm` (no imports, unminified export names, no malloc).

## Files

- `wasm_exports.c` -- Exposes the engine's inline accessors plus a static scratch `Game`
  struct; the server copies each room's state bytes in/out of it (see `workers/src/engine.ts`).
- `build.sh` -- Emscripten build script (`-O3`, `STANDALONE_WASM`, `coup_core.c` + `wasm_exports.c`).
