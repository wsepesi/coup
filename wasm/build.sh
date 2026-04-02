#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
C_DIR="$SCRIPT_DIR/../c_engine"
OUT_DIR="$SCRIPT_DIR"

echo "Compiling C engine to WASM..."

# Build two variants:
# 1. Standalone WASM (for Cloudflare Workers — direct WebAssembly.instantiate)
# 2. Emscripten JS+WASM (for browser offline mode)

# --- Standalone WASM for Cloudflare Workers ---
emcc "$C_DIR/coup_core.c" "$C_DIR/text_render.c" "$SCRIPT_DIR/wasm_exports.c" \
  -I"$C_DIR" \
  -O3 \
  -s EXPORTED_FUNCTIONS='[
    "_game_init",
    "_step_deterministic",
    "_step_with_rng",
    "_get_valid_actions",
    "_observe",
    "_render_text",
    "_parse_text_action",
    "_is_done",
    "_get_winner",
    "_get_active_player_ext",
    "_get_num_players_ext",
    "_get_deck_total",
    "_chance_outcomes",
    "_apply_chance",
    "_is_chance_node",
    "_gamelog_init",
    "_log_action",
    "_log_challenge",
    "_log_challenge_resolve",
    "_log_block",
    "_log_pass",
    "_log_lose_card",
    "_log_exchange",
    "_log_eliminated",
    "_log_game_over",
    "_wasm_get_phase",
    "_wasm_get_turn_player",
    "_wasm_get_active_player",
    "_wasm_get_pending_action",
    "_wasm_get_num_players",
    "_wasm_player_card0_type",
    "_wasm_player_card0_alive",
    "_wasm_player_card1_type",
    "_wasm_player_card1_alive",
    "_wasm_player_coins",
    "_wasm_player_is_alive",
    "_wasm_deck_total",
    "_wasm_get_exchange_card0",
    "_wasm_get_exchange_card1",
    "_wasm_get_blocker",
    "_wasm_get_block_card",
    "_wasm_get_responded_mask",
    "_wasm_game_struct_size",
    "_wasm_gamelog_struct_size",
    "_wasm_history_struct_size",
    "_wasm_textactionmap_struct_size",
    "_wasm_chanceoutcome_struct_size",
    "_wasm_set_refund_on_challenge",
    "_wasm_get_refund_on_challenge",
    "_malloc",
    "_free"
  ]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPF32","HEAPU8","HEAP8","HEAP32","HEAPU32","getValue","setValue"]' \
  -s ALLOW_MEMORY_GROWTH=0 \
  -s INITIAL_MEMORY=1048576 \
  -s STANDALONE_WASM=0 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME='createCoupModule' \
  -s ENVIRONMENT='web,worker' \
  -s FILESYSTEM=0 \
  -o "$OUT_DIR/coup.js"

echo "WASM build complete:"
ls -la "$OUT_DIR/coup.js" "$OUT_DIR/coup.wasm"
