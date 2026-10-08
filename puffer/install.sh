#!/bin/bash
# Link the Coup env into the PufferLib 5.0 submodule (idempotent).
#   ocean/coup/coup.h + c_engine sources -> symlinks into this repo
#   config/coup.ini                      -> symlink to puffer/coup.ini
# The links are added to the submodule's .git/info/exclude so the submodule
# does not show up dirty. Then: cd lib/PufferLib && ./build.sh coup
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PUFFER="$ROOT/lib/PufferLib"

if [ ! -f "$PUFFER/build.sh" ]; then
    echo "PufferLib submodule missing: git submodule update --init lib/PufferLib" >&2
    exit 1
fi

mkdir -p "$PUFFER/ocean/coup"
ln -sfn "$ROOT/puffer/coup.h" "$PUFFER/ocean/coup/coup.h"
for f in coup_core.c coup_core.h prng.h history.h coup_obs.h heuristic.h; do
    ln -sfn "$ROOT/c_engine/$f" "$PUFFER/ocean/coup/$f"
done
ln -sfn "$ROOT/puffer/coup.ini" "$PUFFER/config/coup.ini"

GIT_DIR="$(git -C "$PUFFER" rev-parse --absolute-git-dir 2>/dev/null || true)"
if [ -n "$GIT_DIR" ]; then
    EXCLUDE="$GIT_DIR/info/exclude"
    mkdir -p "$(dirname "$EXCLUDE")"
    touch "$EXCLUDE"
    for p in /ocean/coup/ /config/coup.ini /build/cpu_coup /coup /puffer; do
        grep -qxF "$p" "$EXCLUDE" || echo "$p" >> "$EXCLUDE"
    done
fi

echo "Linked coup into $PUFFER (ocean/coup, config/coup.ini)"
