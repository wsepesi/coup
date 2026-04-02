#ifndef COUP_CORE_H
#define COUP_CORE_H

#include <stdint.h>
#include "prng.h"
#include "history.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Card types */
#define DUKE       0
#define ASSASSIN   1
#define CAPTAIN    2
#define AMBASSADOR 3
#define CONTESSA   4

/* Phases */
#define PHASE_DEAL             0
#define PHASE_CHANCE_REDRAW    1
#define PHASE_CHANCE_EXCHANGE  2
#define PHASE_MAIN_ACTION      3
#define PHASE_CHALLENGE_ACTION 4
#define PHASE_BLOCK            5
#define PHASE_CHALLENGE_BLOCK  6
#define PHASE_LOSE_CARD        7
#define PHASE_EXCHANGE_DISCARD 8
#define PHASE_RESOLVE          9

/* Actions */
#define ACT_INCOME            0
#define ACT_FOREIGN_AID       1
#define ACT_TAX               2
#define ACT_EXCHANGE          3
#define ACT_COUP_P0           4
#define ACT_STEAL_P0         10
#define ACT_ASSASSINATE_P0   16
#define ACT_CHALLENGE        22
#define ACT_PASS             23
#define ACT_BLOCK_CONTESSA   24
#define ACT_BLOCK_CAPTAIN    25
#define ACT_BLOCK_AMBASSADOR 26
#define ACT_BLOCK_DUKE       27
#define ACT_DISCARD_SLOT0    28
#define ACT_DISCARD_SLOT1    29
#define ACT_DISCARD_SLOT2    30
#define ACT_DISCARD_SLOT3    31

#define MAX_CHANCE_OUTCOMES 5
#define MAX_PLAYERS 6

#define FIRST_DISCARD_NONE 7

/* Maximum turns (main actions) before the game ends in a tiebreak.
 * Tiebreaker: most alive cards, then most coins. */
#define MAX_TURNS 200

typedef struct {
    int outcome;
    double prob;
} ChanceOutcome;

typedef struct {
    uint16_t players[6];
    uint16_t deck;
    uint16_t phase_state;
    uint16_t aux;
    uint16_t aux2;
    uint16_t _pad;
    uint16_t turn_count;  /* incremented each main action; game ends at MAX_TURNS */
    Xoshiro256 rng;
} Game;

/* --- Inline bit helpers: player --- */

static inline int player_card0_type(const Game *g, int p) {
    return g->players[p] & 0x7;
}
static inline int player_card0_alive(const Game *g, int p) {
    return (g->players[p] >> 3) & 1;
}
static inline int player_card1_type(const Game *g, int p) {
    return (g->players[p] >> 4) & 0x7;
}
static inline int player_card1_alive(const Game *g, int p) {
    return (g->players[p] >> 7) & 1;
}
static inline int player_coins(const Game *g, int p) {
    return (g->players[p] >> 8) & 0xF;
}
static inline int player_is_alive(const Game *g, int p) {
    return player_card0_alive(g, p) || player_card1_alive(g, p);
}

static inline void set_player_card0_type(Game *g, int p, int t) {
    g->players[p] = (g->players[p] & ~(uint16_t)0x7) | (uint16_t)(t & 0x7);
}
static inline void set_player_card0_alive(Game *g, int p, int a) {
    g->players[p] = (g->players[p] & ~(uint16_t)(1<<3)) | (uint16_t)((a&1)<<3);
}
static inline void set_player_card1_type(Game *g, int p, int t) {
    g->players[p] = (g->players[p] & ~(uint16_t)(0x7<<4)) | (uint16_t)((t&0x7)<<4);
}
static inline void set_player_card1_alive(Game *g, int p, int a) {
    g->players[p] = (g->players[p] & ~(uint16_t)(1<<7)) | (uint16_t)((a&1)<<7);
}
static inline void set_player_coins(Game *g, int p, int c) {
    g->players[p] = (g->players[p] & ~(uint16_t)(0xF<<8)) | (uint16_t)((c&0xF)<<8);
}

/* --- Inline bit helpers: deck --- */

static inline int deck_count(const Game *g, int card_type) {
    return (g->deck >> (card_type * 2)) & 0x3;
}
static inline void deck_set_count(Game *g, int card_type, int count) {
    g->deck = (g->deck & ~(uint16_t)(0x3 << (card_type*2))) | (uint16_t)((count&0x3) << (card_type*2));
}
static inline int deck_total(const Game *g) {
    int t = 0;
    for (int i = 0; i < 5; i++) t += deck_count(g, i);
    return t;
}
static inline void deck_add(Game *g, int card_type) {
    deck_set_count(g, card_type, deck_count(g, card_type) + 1);
}
static inline void deck_remove(Game *g, int card_type) {
    deck_set_count(g, card_type, deck_count(g, card_type) - 1);
}

/* --- Inline bit helpers: phase_state --- */

static inline int get_phase(const Game *g) {
    return g->phase_state & 0xF;
}
static inline void set_phase(Game *g, int phase) {
    g->phase_state = (g->phase_state & ~(uint16_t)0xF) | (uint16_t)(phase & 0xF);
}
static inline int get_turn_player(const Game *g) {
    return (g->phase_state >> 4) & 0x7;
}
static inline void set_turn_player(Game *g, int p) {
    g->phase_state = (g->phase_state & ~(uint16_t)(0x7<<4)) | (uint16_t)((p&0x7)<<4);
}
static inline int get_active_player(const Game *g) {
    return (g->phase_state >> 7) & 0x7;
}
static inline void set_active_player(Game *g, int p) {
    g->phase_state = (g->phase_state & ~(uint16_t)(0x7<<7)) | (uint16_t)((p&0x7)<<7);
}
static inline int get_pending_action(const Game *g) {
    return (g->phase_state >> 10) & 0x3F;
}
static inline void set_pending_action(Game *g, int a) {
    g->phase_state = (g->phase_state & ~(uint16_t)(0x3F<<10)) | (uint16_t)((a&0x3F)<<10);
}

/* --- Inline bit helpers: aux --- */

static inline int get_responded_mask(const Game *g) {
    return g->aux & 0x3F;
}
static inline void set_responded_mask(Game *g, int mask) {
    g->aux = (g->aux & ~(uint16_t)0x3F) | (uint16_t)(mask & 0x3F);
}
static inline int get_exchange_card0(const Game *g) {
    return (g->aux >> 6) & 0x7;
}
static inline void set_exchange_card0(Game *g, int t) {
    g->aux = (g->aux & ~(uint16_t)(0x7<<6)) | (uint16_t)((t&0x7)<<6);
}
static inline int get_exchange_card1(const Game *g) {
    return (g->aux >> 9) & 0x7;
}
static inline void set_exchange_card1(Game *g, int t) {
    g->aux = (g->aux & ~(uint16_t)(0x7<<9)) | (uint16_t)((t&0x7)<<9);
}
static inline int get_first_discard(const Game *g) {
    return (g->aux >> 12) & 0x7;
}
static inline void set_first_discard(Game *g, int d) {
    g->aux = (g->aux & ~(uint16_t)(0x7<<12)) | (uint16_t)((d&0x7)<<12);
}

/* --- Inline bit helpers: aux2 --- */

static inline int get_blocker(const Game *g) {
    return g->aux2 & 0x7;
}
static inline void set_blocker(Game *g, int p) {
    g->aux2 = (g->aux2 & ~(uint16_t)0x7) | (uint16_t)(p & 0x7);
}
static inline int get_block_card(const Game *g) {
    return (g->aux2 >> 3) & 0x7;
}
static inline void set_block_card(Game *g, int c) {
    g->aux2 = (g->aux2 & ~(uint16_t)(0x7<<3)) | (uint16_t)((c&0x7)<<3);
}
static inline int get_num_players(const Game *g) {
    return (g->aux2 >> 6) & 0x7;
}
static inline void set_num_players(Game *g, int n) {
    g->aux2 = (g->aux2 & ~(uint16_t)(0x7<<6)) | (uint16_t)((n&0x7)<<6);
}

/* --- Observation --- */

#define OBS_SIZE 407

int get_observation_size(void);
void observe(const Game *g, int player_id, const HistoryBuffer *history, float *out);

/* Snapshot for incremental observation updates.
 * Stores previous game state so observe_incremental() can diff. */
typedef struct {
    uint16_t players[6];   /* previous player bit-packed state */
    uint16_t phase_state;  /* previous phase + turn + active + pending */
    uint16_t aux;          /* previous responded_mask + exchange cards */
    uint8_t  history_len;  /* previous history length */
    int8_t   player_id;    /* previous observer player (-1 = uninitialized) */
} ObsSnapshot;

/* Incremental observation: only rewrites floats that changed since last call.
 * On first call or after reset, snap->player_id should be -1 to trigger
 * a full recompute. The snapshot is updated at the end of each call. */
void observe_incremental(const Game *g, int player_id,
                         const HistoryBuffer *history,
                         float *out, ObsSnapshot *snap);

/* --- Core API --- */

void game_init(Game *g, int num_players, uint64_t deal_seed, uint64_t proc_seed);
void step_deterministic(Game *g, int action);
void step_with_rng(Game *g, int action);
uint32_t get_valid_actions(const Game *g);
int is_chance_node(const Game *g);
int chance_outcomes(const Game *g, ChanceOutcome *out);
void apply_chance(Game *g, int outcome);
int get_active_player_ext(const Game *g);
int is_done(const Game *g);
int get_winner(const Game *g);
int get_num_players_ext(const Game *g);
int get_deck_total(const Game *g);

#ifdef __cplusplus
}
#endif

#endif /* COUP_CORE_H */
