# WASM

Compiles the C game engine to WebAssembly for use in Cloudflare Workers and browser environments.

## Build

Requires [Emscripten](https://emscripten.org/):

```bash
./build.sh
```

Outputs `coup.wasm` and `coup.js`.

## Files

- `wasm_exports.c` -- Wrapper that exposes C engine inline helpers as WASM-exportable functions (game state accessors, struct sizes, config setters)
- `build.sh` -- Emscripten build script (`-O3`, targets `coup_core.c` + `text_render.c` + `wasm_exports.c`)
