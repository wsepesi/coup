#!/bin/bash
# Writes src/version.ts with the current git hash, only when it changes
# (rewriting it unconditionally makes `wrangler dev` rebuild in a loop).
cd "$(dirname "$0")/.."
line="export const PROTOCOL_VERSION = \"$(git rev-parse --short HEAD 2>/dev/null || echo dev)\";"
[ "$(cat src/version.ts 2>/dev/null)" = "$line" ] || echo "$line" > src/version.ts
