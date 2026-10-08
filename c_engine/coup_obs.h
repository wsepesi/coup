/*
 * coup_obs.h — RL observation + action-space helpers (header-only).
 *
 * Shared by the PufferLib env (puffer/coup.h) and the OpenSpiel adapter
 * (cpp_engine/) so both train/evaluate on the exact same features.
 *
 * Design:
 *   - Egocentric. Seats are rotated so the observer is always relative seat 0
 *     and opponents follow in turn order. One shared policy can then play any
 *     seat without having to learn seat-specific behaviour.
 *   - Relative action space. Target actions (coup/steal/assassinate) name a
 *     RELATIVE target seat, so "steal from the player on my left" is the same
 *     action index from every seat. Use coup_action_to_abs/to_rel to convert.
 *   - uint8, 0/1 features only (one-hot / thermometer). No scalars, so the
 *     buffer can be fed to a network as-is (PufferLib casts uint8 -> float).
 *   - Only public information plus the observer's own private cards and
 *     exchange draws. The tracker records only publicly observable events.
 *
 * Layout (COUP_OBS_SIZE bytes):
 *   [0,   222)  6 relative seats x 37:
 *                 +0  seat exists         +1  seat alive
 *                 +2  card0 alive         +3..7   card0 type (self or revealed)
 *                 +8  card1 alive         +9..13  card1 type (self or revealed)
 *                 +14..26 coins one-hot (0..12)
 *                 +27..31 roles claimed since this player's hand last changed
 *                 +32 turn player  +33 deciding now  +34 target of pending
 *                 +35 blocker      +36 already responded this window
 *   [222, 229)  phase one-hot (main, chal_action, block, chal_block,
 *               lose_card, exchange_discard, other)
 *   [229, 236)  pending action kind one-hot (income, foreign_aid, tax,
 *               exchange, coup, steal, assassinate); zero in main phase
 *   [236, 241)  claimed block card one-hot (while a block is pending)
 *   [241, 255)  own exchange: drawn card 0 (5), drawn card 1 (5),
 *               first discard slot (4); zero unless observer is exchanging
 *   [255, 270)  unseen copies per role from observer's view (excludes own
 *               hand, own exchange draws and revealed cards), thermometer 3x5
 *   [270, 278)  turn-count bucket one-hot (8 buckets over MAX_TURNS)
 *   [278, 283)  player-count one-hot (2..6)
 *   [283, 587)  last 8 non-PASS decisions, newest first, each 38:
 *                 +0..5 relative actor one-hot, +6..37 relative action one-hot
 *               (another player's exchange-discard picks are private: only
 *               the actor bit is set)
 */
#ifndef COUP_OBS_H
#define COUP_OBS_H

#include <string.h>
#include "coup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COUP_NUM_ACTIONS 32
#define COUP_OBS_HIST 8
#define COUP_OBS_SEAT 37
#define COUP_OBS_EVENT 38
#define COUP_OBS_SIZE (6 * COUP_OBS_SEAT + 61 + COUP_OBS_HIST * COUP_OBS_EVENT)
#define COUP_EVENT_HIDDEN 0xFF

typedef struct {
    uint8_t actor;   /* absolute seat */
    uint8_t action;  /* absolute action, or COUP_EVENT_HIDDEN */
} CoupEvent;

typedef struct {
    CoupEvent hist[COUP_OBS_HIST];  /* ring buffer */
    uint8_t head;                   /* next write slot */
    uint8_t len;
    uint8_t claims[MAX_PLAYERS];    /* role bitmask per absolute seat */
} CoupObsTracker;

/* ---- Action-space rotation ---- */

static inline int coup_action_is_targeted(int a) {
    return a >= ACT_COUP_P0 && a < ACT_ASSASSINATE_P0 + 6;
}

/* Relative action (policy space) -> absolute action (engine space). */
static inline int coup_action_to_abs(int a, int me, int np) {
    if (!coup_action_is_targeted(a)) return a;
    int base = a - (a - ACT_COUP_P0) % 6;
    int rel = a - base;
    return base + (rel < np ? (me + rel) % np : rel);
}

/* Absolute action -> relative action as seen by `me`. */
static inline int coup_action_to_rel(int a, int me, int np) {
    if (!coup_action_is_targeted(a)) return a;
    int base = a - (a - ACT_COUP_P0) % 6;
    int abs_t = a - base;
    return base + (abs_t < np ? (abs_t - me + np) % np : abs_t);
}

/* Valid-action mask in relative action space for `me`. Zero when `me` is not
 * the player to act. */
static inline uint32_t coup_valid_actions_rel(const Game *g, int me) {
    if (is_done(g) || get_active_player_ext(g) != me) return 0;
    uint32_t abs_mask = get_valid_actions(g);
    uint32_t rel = abs_mask & ~(((1u << 18) - 1) << ACT_COUP_P0);
    int np = get_num_players(g);
    for (int a = ACT_COUP_P0; a < ACT_ASSASSINATE_P0 + 6; a++) {
        if (abs_mask & (1u << a)) rel |= 1u << coup_action_to_rel(a, me, np);
    }
    return rel;
}

/* ---- Event tracking ---- */

static inline void coup_obs_tracker_reset(CoupObsTracker *t) {
    memset(t, 0, sizeof(*t));
}

static inline int coup_obs_action_role(int a) {
    if (a == ACT_TAX || a == ACT_BLOCK_DUKE) return DUKE;
    if (a == ACT_EXCHANGE || a == ACT_BLOCK_AMBASSADOR) return AMBASSADOR;
    if (a == ACT_BLOCK_CAPTAIN) return CAPTAIN;
    if (a == ACT_BLOCK_CONTESSA) return CONTESSA;
    if (a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6) return CAPTAIN;
    if (a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6) return ASSASSIN;
    return -1;
}

/* Record a decision. Call with the game state BEFORE the action is applied. */
static inline void coup_obs_tracker_record(CoupObsTracker *t, const Game *before,
                                           int actor, int action) {
    int phase = get_phase(before);
    /* Passes carry almost no information (a window that closes without a
     * challenge/block implies them) and would flood the short history in
     * 6-player games, so they are not recorded. */
    if (action == ACT_PASS) return;
    CoupEvent *e = &t->hist[t->head];
    e->actor = (uint8_t)actor;
    e->action = phase == PHASE_EXCHANGE_DISCARD ? COUP_EVENT_HIDDEN : (uint8_t)action;
    t->head = (t->head + 1) % COUP_OBS_HIST;
    if (t->len < COUP_OBS_HIST) t->len++;

    int role = coup_obs_action_role(action);
    if (role >= 0) t->claims[actor] |= (uint8_t)(1u << role);

    if (action == ACT_CHALLENGE) {
        /* A successful defence reveals the claimed card, which is shuffled
         * back and replaced: that role claim is no longer informative. */
        int claimant, role_c;
        if (phase == PHASE_CHALLENGE_ACTION) {
            claimant = get_turn_player(before);
            role_c = coup_obs_action_role(get_pending_action(before));
        } else {
            claimant = get_blocker(before);
            role_c = get_block_card(before);
        }
        int has = (player_card0_alive(before, claimant) &&
                   player_card0_type(before, claimant) == role_c) ||
                  (player_card1_alive(before, claimant) &&
                   player_card1_type(before, claimant) == role_c);
        if (has) t->claims[claimant] &= (uint8_t)~(1u << role_c);
    } else if (phase == PHASE_EXCHANGE_DISCARD &&
               get_first_discard(before) != FIRST_DISCARD_NONE) {
        t->claims[actor] = 0;  /* hand fully reshuffled */
    }
}

/* ---- Observation ---- */

static inline int coup_obs_phase_idx(int phase) {
    switch (phase) {
    case PHASE_MAIN_ACTION:      return 0;
    case PHASE_CHALLENGE_ACTION: return 1;
    case PHASE_BLOCK:            return 2;
    case PHASE_CHALLENGE_BLOCK:  return 3;
    case PHASE_LOSE_CARD:        return 4;
    case PHASE_EXCHANGE_DISCARD: return 5;
    default:                     return 6;
    }
}

static inline int coup_obs_action_kind(int a) {
    if (a <= ACT_EXCHANGE) return a;
    if (a < ACT_STEAL_P0) return 4;
    if (a < ACT_ASSASSINATE_P0) return 5;
    return 6;
}

/* Write the observation for `me` into out[COUP_OBS_SIZE]. `t` may be NULL. */
static inline void coup_obs_write(const Game *g, const CoupObsTracker *t, int me,
                                  uint8_t *out) {
    memset(out, 0, COUP_OBS_SIZE);
    int np = get_num_players(g);
    int phase = get_phase(g);
    int in_window = phase >= PHASE_CHALLENGE_ACTION && phase <= PHASE_CHALLENGE_BLOCK;
    int pending = get_pending_action(g);
    int has_pending = phase != PHASE_MAIN_ACTION && phase != PHASE_DEAL;
    int target = has_pending && coup_action_is_targeted(pending)
        ? (pending - ACT_COUP_P0) % 6 : -1;
    int blocker = phase == PHASE_CHALLENGE_BLOCK ? get_blocker(g) : -1;
    int active = get_active_player_ext(g);
    int responded = get_responded_mask(g);
    /* Slots that hold no real card yet: undealt (PHASE_DEAL, slot index >=
     * deal counter) or the claimant's placeholder during a redraw. */
    int dealt = phase == PHASE_DEAL ? get_pending_action(g) : 2 * MAX_PLAYERS;
    int redraw_slot = phase == PHASE_CHANCE_REDRAW
        ? get_active_player(g) * 2 + get_exchange_card0(g) : -1;
    /* Who may respond in the current window (the rest are pre-marked
     * "responded" by the engine but never had a choice). */
    int window_excluded = phase == PHASE_CHALLENGE_BLOCK ? get_blocker(g)
        : get_turn_player(g);
    int unseen[5] = {3, 3, 3, 3, 3};

    for (int r = 0; r < np; r++) {
        int p = (me + r) % np;
        uint8_t *s = out + r * COUP_OBS_SEAT;
        s[0] = 1;
        s[1] = (uint8_t)player_is_alive(g, p);
        int alive0 = player_card0_alive(g, p), type0 = player_card0_type(g, p);
        int alive1 = player_card1_alive(g, p), type1 = player_card1_type(g, p);
        s[2] = (uint8_t)alive0;
        int real0 = 2 * p < dealt && 2 * p != redraw_slot;
        int real1 = 2 * p + 1 < dealt && 2 * p + 1 != redraw_slot;
        if (real0 && (r == 0 || !alive0)) {
            s[3 + type0] = 1;
            unseen[type0]--;
        }
        s[8] = (uint8_t)alive1;
        if (real1 && (r == 0 || !alive1)) {
            s[9 + type1] = 1;
            unseen[type1]--;
        }
        int coins = player_coins(g, p);
        s[14 + (coins > 12 ? 12 : coins)] = 1;
        uint8_t claims = t ? t->claims[p] : 0;
        for (int c = 0; c < 5; c++) s[27 + c] = (claims >> c) & 1;
        s[32] = p == get_turn_player(g);
        s[33] = p == active;
        s[34] = p == target;
        s[35] = p == blocker;
        int eligible = s[1] && p != window_excluded &&
            (phase != PHASE_BLOCK || target < 0 || p == target);
        s[36] = in_window && eligible && ((responded >> p) & 1);
    }

    uint8_t *gl = out + 6 * COUP_OBS_SEAT;
    gl[coup_obs_phase_idx(phase)] = 1;
    if (has_pending) gl[7 + coup_obs_action_kind(pending)] = 1;
    if (blocker >= 0) gl[14 + get_block_card(g)] = 1;
    if (phase == PHASE_EXCHANGE_DISCARD && get_turn_player(g) == me) {
        int ec0 = get_exchange_card0(g), ec1 = get_exchange_card1(g);
        if (ec0 < 5) gl[19 + ec0] = 1;
        if (ec1 < 5) gl[24 + ec1] = 1;
        if (ec0 < 5) unseen[ec0]--;
        if (ec1 < 5) unseen[ec1]--;
        int fd = get_first_discard(g);
        if (fd < 4) gl[29 + fd] = 1;
    }
    for (int c = 0; c < 5; c++) {
        for (int k = 0; k < unseen[c] && k < 3; k++) gl[33 + c * 3 + k] = 1;
    }
    int bucket = g->turn_count * 8 / MAX_TURNS;
    gl[48 + (bucket > 7 ? 7 : bucket)] = 1;
    gl[56 + np - 2] = 1;

    if (!t) return;
    uint8_t *h = gl + 61;
    for (int i = 0; i < t->len; i++) {
        const CoupEvent *e = &t->hist[(t->head + COUP_OBS_HIST - 1 - i) % COUP_OBS_HIST];
        uint8_t *ev = h + i * COUP_OBS_EVENT;
        ev[(e->actor - me + np) % np] = 1;
        /* Exchange picks are stored hidden for everyone, including the
         * actor: their own hand already shows the result. */
        if (e->action != COUP_EVENT_HIDDEN)
            ev[6 + coup_action_to_rel(e->action, me, np)] = 1;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* COUP_OBS_H */
