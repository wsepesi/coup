#!/bin/bash
# Compile the C engine to a standalone WebAssembly module for the Workers
# game server (workers/src/engine.ts).
#
# STANDALONE_WASM + --no-entry keeps export names unminified, so the JS side
# looks functions up by their real names instead of a hand-maintained map of
# minified symbols that silently breaks whenever the export list changes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
C_DIR="$SCRIPT_DIR/../c_engine"
OUT="$SCRIPT_DIR/coup.wasm"

EXPORTS=(
  game_init step_with_rng get_valid_actions is_done get_winner is_chance_node
  wasm_scratch wasm_game_struct_size
  wasm_get_phase wasm_get_turn_player wasm_get_active_player
  wasm_get_pending_action wasm_get_num_players
  wasm_player_card0_type wasm_player_card0_alive
  wasm_player_card1_type wasm_player_card1_alive
  wasm_player_coins wasm_player_is_alive wasm_deck_total
  wasm_get_exchange_card0 wasm_get_exchange_card1
  wasm_get_blocker wasm_get_block_card wasm_get_responded_mask
  wasm_set_refund_on_challenge wasm_get_refund_on_challenge
)
EXPORT_LIST=$(printf '"_%s",' "${EXPORTS[@]}")
EXPORT_LIST="[${EXPORT_LIST%,}]"

echo "Compiling C engine to WASM..."
emcc "$C_DIR/coup_core.c" "$SCRIPT_DIR/wasm_exports.c" \
  -I"$C_DIR" \
  -O3 \
  --no-entry \
  -s STANDALONE_WASM=1 \
  -s EXPORTED_FUNCTIONS="$EXPORT_LIST" \
  -s ALLOW_MEMORY_GROWTH=0 \
  -s INITIAL_MEMORY=262144 \
  -s STACK_SIZE=65536 \
  -s FILESYSTEM=0 \
  -o "$OUT"

echo "WASM build complete:"
ls -la "$OUT"
