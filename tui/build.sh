#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# Compile shared library
echo "Building libcoup..."
if [[ "$(uname)" == "Darwin" ]]; then
  cc -dynamiclib -fPIC -O3 -std=c99 \
    -o "$ROOT_DIR/c_engine/libcoup.dylib" \
    "$ROOT_DIR/c_engine/coup_core.c"
  echo "Built c_engine/libcoup.dylib"
else
  cc -shared -fPIC -O3 -std=c99 \
    -o "$ROOT_DIR/c_engine/libcoup.so" \
    "$ROOT_DIR/c_engine/coup_core.c"
  echo "Built c_engine/libcoup.so"
fi

# Run TUI
echo "Starting Coup TUI..."
cd "$ROOT_DIR"
bun run tui/src/index.ts "$@"
