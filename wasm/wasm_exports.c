// Wrapper functions for inline helpers that need to be exported from WASM
#include "coup_core.h"
#include "text_render.h"

int wasm_get_phase(const Game *g) { return get_phase(g); }
int wasm_get_turn_player(const Game *g) { return get_turn_player(g); }
int wasm_get_active_player(const Game *g) { return get_active_player(g); }
int wasm_get_pending_action(const Game *g) { return get_pending_action(g); }
int wasm_get_num_players(const Game *g) { return get_num_players(g); }

int wasm_player_card0_type(const Game *g, int p) { return player_card0_type(g, p); }
int wasm_player_card0_alive(const Game *g, int p) { return player_card0_alive(g, p); }
int wasm_player_card1_type(const Game *g, int p) { return player_card1_type(g, p); }
int wasm_player_card1_alive(const Game *g, int p) { return player_card1_alive(g, p); }
int wasm_player_coins(const Game *g, int p) { return player_coins(g, p); }
int wasm_player_is_alive(const Game *g, int p) { return player_is_alive(g, p); }

int wasm_deck_total(const Game *g) { return deck_total(g); }

int wasm_get_exchange_card0(const Game *g) { return get_exchange_card0(g); }
int wasm_get_exchange_card1(const Game *g) { return get_exchange_card1(g); }
int wasm_get_blocker(const Game *g) { return get_blocker(g); }
int wasm_get_block_card(const Game *g) { return get_block_card(g); }
int wasm_get_responded_mask(const Game *g) { return get_responded_mask(g); }

int wasm_game_struct_size(void) { return (int)sizeof(Game); }
int wasm_gamelog_struct_size(void) { return (int)sizeof(GameLog); }
int wasm_history_struct_size(void) { return (int)sizeof(HistoryBuffer); }
int wasm_textactionmap_struct_size(void) { return (int)sizeof(TextActionMap); }
int wasm_chanceoutcome_struct_size(void) { return (int)sizeof(ChanceOutcome); }

void wasm_set_refund_on_challenge(Game *g, int flag) { game_set_refund_on_challenge(g, flag); }
int wasm_get_refund_on_challenge(const Game *g) { return game_get_refund_on_challenge(g); }
