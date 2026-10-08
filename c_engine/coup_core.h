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
/* PHASE_RESOLVE is never observable: actions resolve within the step that
 * completes them. Kept only so the constant stays stable for other layers. */
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

/* Maximum turns (completed main actions) before the game ends in a tiebreak.
 * House rule, not in the official rulebook. Tiebreaker: most alive cards,
 * then most coins, then lowest seat index. */
#define MAX_TURNS 200

typedef struct {
    int outcome;
    double prob;
} ChanceOutcome;

/* Complete game state: 24 bytes of packed fields + 32 bytes of RNG (56 total).
 * The layout is part of the ABI (the WASM/Workers layer copies raw bytes and
 * pokes _pad bit 13 directly), so do not reorder or resize fields. */
typedef struct {
    uint16_t players[6];  /* per seat: [0:3) card0 type, [3] card0 alive,
                             [4:7) card1 type, [7] card1 alive, [8:12) coins.
                             Unused seats are all-zero (both cards dead). */
    uint16_t deck;        /* 5 x 2-bit counts of each card type in the deck */
    uint16_t phase_state; /* [0:4) phase, [4:7) turn player, [7:10) active
                             player, [10:16) pending action (deal counter in
                             PHASE_DEAL) */
    uint16_t aux;         /* [0:6) responded mask, [6:9) exchange card0 (also
                             the redraw slot), [9:12) exchange card1,
                             [12:15) first exchange discard */
    uint16_t aux2;        /* [0:3) blocker, [3:6) block card, [6:9) num players */
    uint16_t _pad;        /* NOT padding (name kept for ABI): engine-internal.
                             [0:4) lose-card continuation, [7:10) claimant,
                             [10:13) claimed card, [13] refund-on-challenge */
    uint16_t turn_count;  /* incremented each completed turn; ends at MAX_TURNS */
    Xoshiro256 rng;       /* procedural RNG used by step_with_rng() */
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
    return (g->players[p] & 0x88) != 0;
}
/* Does player p hold card_type face-down (i.e. as a living influence)? */
static inline int player_has_card(const Game *g, int p, int card_type) {
    return (player_card0_alive(g, p) && player_card0_type(g, p) == card_type) ||
           (player_card1_alive(g, p) && player_card1_type(g, p) == card_type);
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
    /* Sum of five 2-bit fields: low bits count once, high bits twice. */
    return __builtin_popcount(g->deck & 0x155u) +
           2 * __builtin_popcount(g->deck & 0x2AAu);
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

/* --- Observation (legacy float layout, absolute seats) ---
 * New RL code should use coup_obs.h (egocentric, uint8). This 407-float
 * layout is kept stable for the TS/WASM bots that parse it. */

#define OBS_SIZE 407

int get_observation_size(void);
void observe(const Game *g, int player_id, const HistoryBuffer *history, float *out);

/* --- Core API --- */

/* Initialise a game for num_players (2..6) seats.
 *
 * Every seat starts with 2 coins (2-player: seat 0, who moves first, gets 1). proc_seed seeds g->rng, which
 * step_with_rng() uses for all later chance events (redraws, exchanges).
 *
 *  - If deal_seed or proc_seed is non-zero, the opening hands are dealt
 *    immediately from a separate RNG seeded with deal_seed, and the game is
 *    returned in PHASE_MAIN_ACTION with seat 0 to act. (deal_seed == 0 is a
 *    valid deal seed in that case.)
 *  - If both seeds are 0 the game is left in PHASE_DEAL: 2*num_players chance
 *    nodes the caller resolves with chance_outcomes()/apply_chance() (for
 *    full chance enumeration, e.g. CFR) or step_with_rng(). */
void game_init(Game *g, int num_players, uint64_t deal_seed, uint64_t proc_seed);

/* Apply one decision by the active player.
 *
 * Contract: action must be set in get_valid_actions(g). Returns 0 on success.
 * An invalid action (out of range, not in the mask, at a chance node, or
 * after the game is over) returns -1 and leaves the state untouched. */
int step_deterministic(Game *g, int action);

/* step_deterministic(), then resolve any chance nodes that follow by
 * sampling from g->rng. Also resolves pending chance nodes when called at a
 * chance node (the action is then ignored and -1 returned). After it returns
 * the game is either done or waiting on a player decision. */
int step_with_rng(Game *g, int action);

/* Bitmask of legal actions for the active player (bit i = action i).
 * 0 at chance nodes and once the game is over. */
uint32_t get_valid_actions(const Game *g);
int is_chance_node(const Game *g);
/* Writes up to MAX_CHANCE_OUTCOMES (card type, probability) pairs; returns
 * the count. Only meaningful at a chance node. */
int chance_outcomes(const Game *g, ChanceOutcome *out);
/* Resolve a chance node with the given card type. Returns 0, or -1 (state
 * untouched) if not at a chance node or that card is not in the deck. */
int apply_chance(Game *g, int outcome);
int get_active_player_ext(const Game *g);
int is_done(const Game *g);
int get_winner(const Game *g);
int get_num_players_ext(const Game *g);
int get_deck_total(const Game *g);

/* Block state (for display during PHASE_CHALLENGE_BLOCK) */
int get_blocker_ext(const Game *g);
int get_block_card_ext(const Game *g);

/* House rules */
void game_set_refund_on_challenge(Game *g, int flag);
int game_get_refund_on_challenge(const Game *g);

#ifdef __cplusplus
}
#endif

#endif /* COUP_CORE_H */
