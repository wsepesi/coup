/*
 * coup_core.c — Coup game engine (rules, chance nodes, legacy observation).
 *
 * Turn structure (one step = one decision):
 *
 *   MAIN_ACTION ── income / coup ─────────────────────────────────▶ resolve
 *        │
 *        ├─ foreign aid ─────────────────────────────▶ BLOCK (anyone, Duke)
 *        └─ tax / exchange / steal / assassinate ─▶ CHALLENGE_ACTION (anyone)
 *                                                       │ all pass, or
 *                                                       │ challenge failed
 *                                                       ▼
 *                       steal / assassinate ─▶ BLOCK (target only)
 *                       tax / exchange ──────▶ resolve
 *
 *   BLOCK: all pass ─▶ resolve;  block ─▶ CHALLENGE_BLOCK (anyone but blocker)
 *   CHALLENGE_BLOCK: all pass ─▶ block stands, turn ends
 *
 * A challenge is resolved immediately: the loser enters LOSE_CARD; the
 * _pad "lose-card continuation" says what happens once they discard. A
 * claimant who proves their card returns it to the deck and redraws
 * (CHANCE_REDRAW). Exchange draws two cards (CHANCE_EXCHANGE x2) and then
 * discards two (EXCHANGE_DISCARD x2).
 */
#include "coup_core.h"
#include <string.h>

/* ---- Internal helpers ---- */

static inline int is_coup(int a)        { return a >= ACT_COUP_P0 && a < ACT_COUP_P0 + 6; }
static inline int is_steal(int a)       { return a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6; }
static inline int is_assassinate(int a) { return a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6; }

/* Bitmask of seats with at least one living influence. */
static inline int alive_mask(const Game *g) {
    int m = 0;
    for (int i = 0, np = get_num_players(g); i < np; i++)
        m |= player_is_alive(g, i) << i;
    return m;
}

static inline int dead_mask(const Game *g) {
    return ~alive_mask(g) & 0x3F; /* includes non-existent seats */
}

/* First seat in `mask` strictly after `after` in seating order (wrapping);
 * `after` itself is only returned last. -1 if mask is empty. */
static inline int next_in_mask(int mask, int after) {
    int later = mask & ~((2 << after) - 1);
    if (later) return __builtin_ctz((unsigned)later);
    if (mask) return __builtin_ctz((unsigned)mask);
    return -1;
}

static void advance_turn(Game *g) {
    g->turn_count++;
    int tp = get_turn_player(g);
    int next = next_in_mask(alive_mask(g) & ~(1 << tp), tp);
    if (next < 0) next = tp;
    set_turn_player(g, next);
    set_active_player(g, next);
    set_phase(g, PHASE_MAIN_ACTION);
    set_pending_action(g, 0);
    set_responded_mask(g, dead_mask(g));
    set_first_discard(g, FIRST_DISCARD_NONE);
}

/* Which card is claimed by a given action (-1 for unclaimed actions) */
static int claimed_card_for_action(int action) {
    if (action == ACT_TAX) return DUKE;
    if (action == ACT_EXCHANGE) return AMBASSADOR;
    if (is_steal(action)) return CAPTAIN;
    if (is_assassinate(action)) return ASSASSIN;
    return -1;
}

static int claimed_card_for_block(int block_action) {
    switch (block_action) {
    case ACT_BLOCK_DUKE:       return DUKE;
    case ACT_BLOCK_CAPTAIN:    return CAPTAIN;
    case ACT_BLOCK_AMBASSADOR: return AMBASSADOR;
    case ACT_BLOCK_CONTESSA:   return CONTESSA;
    default: return -1;
    }
}

/* ---- Engine-internal state in Game._pad ---- */

/* What happens after the player in PHASE_LOSE_CARD discards. The values are
 * persisted in serialized games (Workers), so never renumber them. */
#define LC_ADVANCE_TURN        0 /* turn over (coup, assassination, failed bluff) */
#define LC_CLAIMANT_REDRAW     1 /* action challenger lost: claimant redraws, action continues */
#define LC_BLOCK_STANDS        2 /* block challenger lost: blocker redraws, action cancelled */
#define LC_BLOCK_FAILED        3 /* blocker was bluffing: action resolves */
/* 4, 5 were LC_RESOLVE_ASSASSINATE / LC_RESOLVE_COUP; identical to 0. */

static int get_lose_card_context(const Game *g) {
    return g->_pad & 0xF;
}
static void set_lose_card_context(Game *g, int ctx) {
    g->_pad = (g->_pad & ~(uint16_t)0xF) | (uint16_t)(ctx & 0xF);
}
static int get_claimant(const Game *g) {
    return (g->_pad >> 7) & 0x7;
}
static void set_claimant(Game *g, int p) {
    g->_pad = (g->_pad & ~(uint16_t)(0x7 << 7)) | (uint16_t)((p & 0x7) << 7);
}
static int get_claimed_card_stored(const Game *g) {
    return (g->_pad >> 10) & 0x7;
}
static void set_claimed_card_stored(Game *g, int c) {
    g->_pad = (g->_pad & ~(uint16_t)(0x7 << 10)) | (uint16_t)((c & 0x7) << 10);
}
/* Official rule: coins paid for an action are returned when the action is
 * cancelled by a successful challenge (only Assassinate has a cost). */
static int get_refund_on_challenge(const Game *g) {
    return (g->_pad >> 13) & 0x1;
}
static void set_refund_on_challenge(Game *g, int flag) {
    g->_pad = (g->_pad & ~(uint16_t)(0x1 << 13)) | (uint16_t)((flag & 0x1) << 13);
}

/* ---- Transitions ---- */

static void resolve_action(Game *g);

/* Open a response window in `phase` for every living seat except `exclude`,
 * with the first decision going to the first eligible seat at or after
 * `first`. Returns 0 (and changes nothing) if nobody is eligible. */
static int open_window(Game *g, int phase, int exclude, int first) {
    int eligible = alive_mask(g) & ~(1 << exclude);
    if (!eligible) return 0;
    set_responded_mask(g, ~eligible & 0x3F);
    set_phase(g, phase);
    set_active_player(g, next_in_mask(eligible, (first + MAX_PLAYERS - 1) % MAX_PLAYERS));
    return 1;
}

/* Record that the active player passed. Returns the next seat to respond
 * (after the active player in seating order), or -1 if the window closed. */
static int pass_and_next(Game *g) {
    int ap = get_active_player(g);
    int responded = get_responded_mask(g) | (1 << ap);
    set_responded_mask(g, responded);
    int next = next_in_mask(alive_mask(g) & ~responded, ap);
    if (next >= 0) set_active_player(g, next);
    return next;
}

/* The action's claim stands (unchallenged, or the challenger lost):
 * offer the block to the target if any, else resolve. */
static void action_claim_stands(Game *g) {
    int pa = get_pending_action(g);
    int tp = get_turn_player(g);
    if (pa == ACT_FOREIGN_AID) {
        /* Not challengeable, so not reached in practice; kept for safety. */
        if (!open_window(g, PHASE_BLOCK, tp, tp + 1)) resolve_action(g);
    } else if (is_steal(pa) || is_assassinate(pa)) {
        int target = (pa - ACT_COUP_P0) % 6;
        if (player_is_alive(g, target)) {
            /* Only the target may block. */
            set_phase(g, PHASE_BLOCK);
            set_active_player(g, target);
            set_responded_mask(g, 0x3F & ~(1 << target));
        } else {
            resolve_action(g); /* target died challenging */
        }
    } else {
        resolve_action(g); /* tax, exchange: unblockable */
    }
}

/* A challenged claim was true: the card goes back to the deck and the
 * claimant draws a replacement (PHASE_CHANCE_REDRAW). */
static void begin_redraw(Game *g, int p, int card) {
    deck_add(g, card);
    int slot = (player_card0_alive(g, p) && player_card0_type(g, p) == card) ? 0 : 1;
    /* Placeholder type 0 until the chance node fills the slot. */
    if (slot == 0) set_player_card0_type(g, p, 0);
    else           set_player_card1_type(g, p, 0);
    set_exchange_card0(g, slot); /* redraw slot is stashed in exchange_card0 */
    set_phase(g, PHASE_CHANCE_REDRAW);
    set_active_player(g, p);
}

static void begin_lose_card(Game *g, int p, int ctx) {
    set_phase(g, PHASE_LOSE_CARD);
    set_active_player(g, p);
    set_lose_card_context(g, ctx);
}

static void after_lose_card(Game *g) {
    if (__builtin_popcount((unsigned)alive_mask(g)) <= 1) return; /* game over */
    switch (get_lose_card_context(g)) {
    case LC_CLAIMANT_REDRAW:
        begin_redraw(g, get_claimant(g), get_claimed_card_stored(g));
        break;
    case LC_BLOCK_STANDS:
        begin_redraw(g, get_blocker(g), get_block_card(g));
        /* after the redraw the blocked action is cancelled */
        set_lose_card_context(g, LC_ADVANCE_TURN);
        break;
    case LC_BLOCK_FAILED:
        resolve_action(g);
        break;
    default: /* LC_ADVANCE_TURN and legacy values */
        advance_turn(g);
        break;
    }
}

static void after_redraw(Game *g) {
    if (get_lose_card_context(g) == LC_ADVANCE_TURN)
        advance_turn(g);       /* block stood */
    else
        action_claim_stands(g); /* action challenge failed: action continues */
}

static int add_coins(int coins, int n) {
    /* Max reachable is 12 (9 + tax): you must coup at 10+, and coins only
     * grow on your own turn. The clamp just protects the 4-bit field. */
    coins += n;
    return coins > 15 ? 15 : coins;
}

/* Carry out the pending action (all challenges/blocks are settled). */
static void resolve_action(Game *g) {
    int pa = get_pending_action(g);
    int tp = get_turn_player(g);

    if (pa == ACT_INCOME || pa == ACT_FOREIGN_AID || pa == ACT_TAX) {
        static const int gain[3] = {1, 2, 3};
        set_player_coins(g, tp, add_coins(player_coins(g, tp), gain[pa]));
        advance_turn(g);
    } else if (pa == ACT_EXCHANGE) {
        set_phase(g, PHASE_CHANCE_EXCHANGE);
        set_active_player(g, tp);
        set_exchange_card0(g, 7); /* sentinel: not yet drawn */
        set_exchange_card1(g, 7);
    } else if (is_steal(pa)) {
        /* A target eliminated mid-turn (by losing a challenge) is out of the
         * game along with their coins: nothing to steal, like coup/assassinate. */
        int target = pa - ACT_STEAL_P0;
        int amount = player_is_alive(g, target) ? player_coins(g, target) : 0;
        if (amount > 2) amount = 2;
        set_player_coins(g, target, player_coins(g, target) - amount);
        set_player_coins(g, tp, add_coins(player_coins(g, tp), amount));
        advance_turn(g);
    } else { /* coup or assassinate */
        int target = (pa - ACT_COUP_P0) % 6;
        if (player_is_alive(g, target))
            begin_lose_card(g, target, LC_ADVANCE_TURN);
        else
            advance_turn(g);
    }
}

/* ---- Deal ----
 * During PHASE_DEAL the pending-action bits hold the deal counter
 * (0 .. 2*num_players-1) and active_player is the seat being dealt to. */

static void deal_advance(Game *g) {
    int counter = get_pending_action(g) + 1;
    if (counter >= get_num_players(g) * 2) {
        set_pending_action(g, 0);
        set_turn_player(g, 0);
        set_active_player(g, 0);
        set_phase(g, PHASE_MAIN_ACTION);
        set_responded_mask(g, dead_mask(g));
        set_first_discard(g, FIRST_DISCARD_NONE);
    } else {
        set_pending_action(g, counter);
        set_active_player(g, counter / 2);
    }
}

/* Draw a card type uniformly from the deck (-1 if empty). This exact sampler
 * is shared by dealing and step_with_rng; changing it changes every seeded
 * game, so don't. */
static int sample_card(const Game *g, Xoshiro256 *rng) {
    int total = deck_total(g);
    if (total == 0) return -1;
    int r = (int)xoshiro256_uniform(rng, (uint32_t)total);
    for (int i = 0; i < 5; i++) {
        r -= deck_count(g, i);
        if (r < 0) return i;
    }
    return -1; /* unreachable */
}

/* Resolve chance nodes from rng until a decision node or the end. */
static void resolve_chance(Game *g, Xoshiro256 *rng) {
    while (is_chance_node(g) && !is_done(g)) {
        int card = sample_card(g, rng);
        if (card < 0 || apply_chance(g, card) != 0) break;
    }
}

/* ---- Public API ---- */

void game_init(Game *g, int num_players, uint64_t deal_seed, uint64_t proc_seed) {
    memset(g, 0, sizeof(Game));
    if (num_players < 2) num_players = 2;
    if (num_players > MAX_PLAYERS) num_players = MAX_PLAYERS;
    set_num_players(g, num_players);

    for (int i = 0; i < 5; i++)
        deck_set_count(g, i, 3); /* 3 of each role = 15 cards */

    /* Cards start dead; dealing brings them alive. Unused seats stay 0.
     * Official 2-player rule: the starting player (seat 0) gets 1 coin. */
    for (int i = 0; i < num_players; i++)
        set_player_coins(g, i, num_players == 2 && i == 0 ? 1 : 2);

    set_phase(g, PHASE_DEAL);
    set_active_player(g, 0);
    set_pending_action(g, 0);
    set_first_discard(g, FIRST_DISCARD_NONE);
    set_refund_on_challenge(g, 1);

    xoshiro256_seed(&g->rng, proc_seed);

    if (deal_seed != 0 || proc_seed != 0) {
        Xoshiro256 deal_rng;
        xoshiro256_seed(&deal_rng, deal_seed);
        resolve_chance(g, &deal_rng);
    }
}

int is_chance_node(const Game *g) {
    int ph = get_phase(g);
    return ph == PHASE_DEAL || ph == PHASE_CHANCE_REDRAW || ph == PHASE_CHANCE_EXCHANGE;
}

int chance_outcomes(const Game *g, ChanceOutcome *out) {
    int total = deck_total(g);
    if (total == 0) return 0;
    int n = 0;
    for (int i = 0; i < 5; i++) {
        int c = deck_count(g, i);
        if (c > 0) {
            out[n].outcome = i;
            out[n].prob = (double)c / (double)total;
            n++;
        }
    }
    return n;
}

int apply_chance(Game *g, int outcome) {
    if (!is_chance_node(g) || outcome < 0 || outcome >= 5 || deck_count(g, outcome) == 0)
        return -1;
    deck_remove(g, outcome);
    switch (get_phase(g)) {
    case PHASE_DEAL: {
        int counter = get_pending_action(g);
        int p = counter / 2;
        if (counter % 2 == 0) {
            set_player_card0_type(g, p, outcome);
            set_player_card0_alive(g, p, 1);
        } else {
            set_player_card1_type(g, p, outcome);
            set_player_card1_alive(g, p, 1);
        }
        deal_advance(g);
        break;
    }
    case PHASE_CHANCE_REDRAW: {
        int p = get_active_player(g);
        if (get_exchange_card0(g) == 0) set_player_card0_type(g, p, outcome);
        else                            set_player_card1_type(g, p, outcome);
        after_redraw(g);
        break;
    }
    case PHASE_CHANCE_EXCHANGE:
        if (get_exchange_card0(g) == 7) {
            set_exchange_card0(g, outcome);
        } else {
            set_exchange_card1(g, outcome);
            set_phase(g, PHASE_EXCHANGE_DISCARD);
            set_active_player(g, get_turn_player(g));
            set_first_discard(g, FIRST_DISCARD_NONE);
        }
        break;
    }
    return 0;
}

/* Called when the active player challenges `claimant`'s claim of `card`. */
static void challenge(Game *g, int claimant, int card, int ctx_if_true, int ctx_if_false) {
    int challenger = get_active_player(g);
    set_claimant(g, claimant);
    set_claimed_card_stored(g, card);
    if (player_has_card(g, claimant, card))
        begin_lose_card(g, challenger, ctx_if_true);
    else
        begin_lose_card(g, claimant, ctx_if_false);
}

int step_deterministic(Game *g, int action) {
    if ((unsigned)action >= 32u || !((get_valid_actions(g) >> action) & 1u))
        return -1;

    int tp = get_turn_player(g);

    switch (get_phase(g)) {
    case PHASE_MAIN_ACTION:
        set_pending_action(g, action);
        if (action == ACT_INCOME) {
            resolve_action(g);
        } else if (is_coup(action)) {
            set_player_coins(g, tp, player_coins(g, tp) - 7);
            resolve_action(g);
        } else if (action == ACT_FOREIGN_AID) {
            /* Unclaimed, so unchallengeable; anyone may block with Duke. */
            if (!open_window(g, PHASE_BLOCK, tp, tp + 1)) resolve_action(g);
        } else {
            if (is_assassinate(action))
                set_player_coins(g, tp, player_coins(g, tp) - 3); /* paid up front */
            if (!open_window(g, PHASE_CHALLENGE_ACTION, tp, tp + 1))
                action_claim_stands(g);
        }
        break;

    case PHASE_CHALLENGE_ACTION:
        if (action == ACT_PASS) {
            if (pass_and_next(g) < 0) action_claim_stands(g);
        } else { /* ACT_CHALLENGE */
            int pa = get_pending_action(g);
            int claimed = claimed_card_for_action(pa);
            if (!player_has_card(g, tp, claimed) && is_assassinate(pa) &&
                get_refund_on_challenge(g))
                set_player_coins(g, tp, player_coins(g, tp) + 3);
            challenge(g, tp, claimed, LC_CLAIMANT_REDRAW, LC_ADVANCE_TURN);
        }
        break;

    case PHASE_BLOCK:
        if (action == ACT_PASS) {
            /* Foreign aid: every opponent gets a chance. Steal/assassinate:
             * the window holds only the target, so it closes here. */
            if (pass_and_next(g) < 0) resolve_action(g);
        } else { /* a block */
            int blocker = get_active_player(g);
            set_blocker(g, blocker);
            set_block_card(g, claimed_card_for_block(action));
            /* Anyone but the blocker may challenge, turn player first. */
            if (!open_window(g, PHASE_CHALLENGE_BLOCK, blocker, tp))
                advance_turn(g);
        }
        break;

    case PHASE_CHALLENGE_BLOCK:
        if (action == ACT_PASS) {
            if (pass_and_next(g) < 0) advance_turn(g); /* block stands */
        } else { /* ACT_CHALLENGE */
            challenge(g, get_blocker(g), get_block_card(g), LC_BLOCK_STANDS, LC_BLOCK_FAILED);
        }
        break;

    case PHASE_LOSE_CARD: {
        int ap = get_active_player(g);
        if (action == ACT_DISCARD_SLOT0) set_player_card0_alive(g, ap, 0);
        else                             set_player_card1_alive(g, ap, 0);
        after_lose_card(g);
        break;
    }

    case PHASE_EXCHANGE_DISCARD: {
        int slot = action - ACT_DISCARD_SLOT0; /* 0,1 = hand; 2,3 = drawn */
        int first = get_first_discard(g);
        if (first == FIRST_DISCARD_NONE) {
            set_first_discard(g, slot); /* wait for the second pick */
            break;
        }
        int cards[4] = {
            player_card0_type(g, tp), player_card1_type(g, tp),
            get_exchange_card0(g), get_exchange_card1(g),
        };
        int keep = 0xF & ~(1 << first) & ~(1 << slot);
        if (!player_card0_alive(g, tp)) keep &= ~1; /* dead cards stay put */
        if (!player_card1_alive(g, tp)) keep &= ~2;
        deck_add(g, cards[first]);
        deck_add(g, cards[slot]);
        /* Refill the living hand slots with the kept cards, in slot order. */
        if (player_card0_alive(g, tp)) {
            int k = __builtin_ctz((unsigned)keep);
            set_player_card0_type(g, tp, cards[k]);
            keep &= keep - 1;
        }
        if (player_card1_alive(g, tp))
            set_player_card1_type(g, tp, cards[__builtin_ctz((unsigned)keep)]);
        advance_turn(g);
        break;
    }
    }
    return 0;
}

int step_with_rng(Game *g, int action) {
    int rc = step_deterministic(g, action);
    resolve_chance(g, &g->rng);
    return rc;
}

uint32_t get_valid_actions(const Game *g) {
    if (is_done(g)) return 0;
    uint32_t mask = 0;

    switch (get_phase(g)) {
    case PHASE_MAIN_ACTION: {
        int tp = get_turn_player(g);
        int coins = player_coins(g, tp);
        uint32_t targets = (uint32_t)(alive_mask(g) & ~(1 << tp));

        if (coins >= 10) /* must coup */
            return targets << ACT_COUP_P0;
        mask = (1u << ACT_INCOME) | (1u << ACT_FOREIGN_AID) |
               (1u << ACT_TAX) | (1u << ACT_EXCHANGE) |
               (targets << ACT_STEAL_P0);
        if (coins >= 7) mask |= targets << ACT_COUP_P0;
        if (coins >= 3) mask |= targets << ACT_ASSASSINATE_P0;
        break;
    }

    case PHASE_CHALLENGE_ACTION:
    case PHASE_CHALLENGE_BLOCK:
        mask = (1u << ACT_CHALLENGE) | (1u << ACT_PASS);
        break;

    case PHASE_BLOCK: {
        int pa = get_pending_action(g);
        mask = 1u << ACT_PASS;
        if (pa == ACT_FOREIGN_AID)
            mask |= 1u << ACT_BLOCK_DUKE;
        else if (is_steal(pa))
            mask |= (1u << ACT_BLOCK_CAPTAIN) | (1u << ACT_BLOCK_AMBASSADOR);
        else if (is_assassinate(pa))
            mask |= 1u << ACT_BLOCK_CONTESSA;
        break;
    }

    case PHASE_LOSE_CARD: {
        int ap = get_active_player(g);
        mask = ((uint32_t)player_card0_alive(g, ap) << ACT_DISCARD_SLOT0) |
               ((uint32_t)player_card1_alive(g, ap) << ACT_DISCARD_SLOT1);
        break;
    }

    case PHASE_EXCHANGE_DISCARD: {
        /* Discard two of the 4 slots (0,1 = living hand cards, 2,3 = drawn)
         * as an ordered pair first < second, so each choice of two discards
         * has exactly one action sequence. */
        int tp = get_turn_player(g);
        uint32_t avail = (uint32_t)player_card0_alive(g, tp) |
                         ((uint32_t)player_card1_alive(g, tp) << 1) | 0xCu;
        int first = get_first_discard(g);
        if (first == FIRST_DISCARD_NONE)
            avail &= 0x7u; /* slot 3 can't be first: nothing above it */
        else
            avail &= ~((2u << first) - 1);
        mask = avail << ACT_DISCARD_SLOT0;
        break;
    }

    default: /* chance nodes */
        break;
    }

    return mask;
}

int get_active_player_ext(const Game *g) {
    if (is_chance_node(g)) return -1;
    return get_active_player(g);
}

int is_done(const Game *g) {
    if (get_phase(g) == PHASE_DEAL) return 0;
    if (g->turn_count >= MAX_TURNS) return 1;
    return __builtin_popcount((unsigned)alive_mask(g)) <= 1;
}

int get_winner(const Game *g) {
    int alive = alive_mask(g);
    if (!alive) return -1;

    /* Sole survivor, or the MAX_TURNS tiebreak: most living cards, then most
     * coins, then lowest seat. */
    int best = -1, best_key = -1;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!((alive >> p) & 1)) continue;
        int key = (player_card0_alive(g, p) + player_card1_alive(g, p)) * 16 +
                  player_coins(g, p);
        if (key > best_key) { best = p; best_key = key; }
    }
    return best;
}

int get_num_players_ext(const Game *g) {
    return get_num_players(g);
}

int get_deck_total(const Game *g) {
    return deck_total(g);
}

int get_blocker_ext(const Game *g) {
    return get_blocker(g);
}

int get_block_card_ext(const Game *g) {
    return get_block_card(g);
}

void game_set_refund_on_challenge(Game *g, int flag) {
    set_refund_on_challenge(g, flag);
}

int game_get_refund_on_challenge(const Game *g) {
    return get_refund_on_challenge(g);
}

/* ---- Observation ---- */

int get_observation_size(void) {
    return OBS_SIZE;
}

void observe(const Game *g, int player_id, const HistoryBuffer *history,
             float *out)
{
    memset(out, 0, OBS_SIZE * sizeof(float));
    int off = 0;

    /* All players' cards — absolute encoding (6 players x 12 floats = 72)
     * Per card: type one-hot (5) + alive flag (1)
     * Type is visible if: (a) this is the observer's card, or (b) card is dead.
     * Alive flag is always public. */
    int np = get_num_players(g);
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (p < np) {
            int is_self = (p == player_id);
            int c0_alive = player_card0_alive(g, p);
            if (is_self || !c0_alive)
                out[off + player_card0_type(g, p)] = 1.0f;
            out[off + 5] = (float)c0_alive;

            int c1_alive = player_card1_alive(g, p);
            if (is_self || !c1_alive)
                out[off + 6 + player_card1_type(g, p)] = 1.0f;
            out[off + 11] = (float)c1_alive;
        }
        off += 12;
    }
    /* off == 72 */

    /* Coins for all 6 slots, normalized by 12 */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i < np)
            out[off + i] = (float)player_coins(g, i) / 12.0f;
    }
    off += 6;
    /* off == 78 */

    /* Alive mask */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i < np)
            out[off + i] = player_is_alive(g, i) ? 1.0f : 0.0f;
    }
    off += 6;
    /* off == 84 */

    /* Phase one-hot (7 floats) */
    {
        int phase = get_phase(g);
        int idx;
        switch (phase) {
            case PHASE_MAIN_ACTION:      idx = 0; break;
            case PHASE_CHALLENGE_ACTION: idx = 1; break;
            case PHASE_BLOCK:            idx = 2; break;
            case PHASE_CHALLENGE_BLOCK:  idx = 3; break;
            case PHASE_LOSE_CARD:        idx = 4; break;
            case PHASE_EXCHANGE_DISCARD: idx = 5; break;
            default:                     idx = 6; break;
        }
        out[off + idx] = 1.0f;
    }
    off += 7;
    /* off == 91 */

    /* Active player one-hot (6) */
    {
        int ap = get_active_player(g);
        if (ap < MAX_PLAYERS)
            out[off + ap] = 1.0f;
    }
    off += 6;
    /* off == 97 */

    /* Turn player one-hot (6) */
    {
        int tp = get_turn_player(g);
        if (tp < MAX_PLAYERS)
            out[off + tp] = 1.0f;
    }
    off += 6;
    /* off == 103 */

    /* Pending action one-hot (32) */
    {
        int pa = get_pending_action(g);
        if (pa < 32)
            out[off + pa] = 1.0f;
    }
    off += 32;
    /* off == 135 */

    /* Responded mask (6) */
    {
        int rm = get_responded_mask(g);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            out[off + i] = (rm >> i) & 1 ? 1.0f : 0.0f;
        }
    }
    off += 6;
    /* off == 141 */

    /* Exchange cards (10 floats: 2 x 5 one-hot) */
    if (get_phase(g) == PHASE_EXCHANGE_DISCARD &&
        get_active_player(g) == player_id) {
        int ec0 = get_exchange_card0(g);
        int ec1 = get_exchange_card1(g);
        if (ec0 < 5) out[off + ec0] = 1.0f;
        if (ec1 < 5) out[off + 5 + ec1] = 1.0f;
    }
    off += 10;
    /* off == 151 */

    /* History (64 entries x 4 floats = 256)
     * Stored newest-first: slot 0 = most recent action.
     * Read directly from ring buffer — no reversal needed. */
    if (history) {
        int n = history->len;
        if (n > 64) n = 64;
        for (int i = 0; i < n; i++) {
            HistoryEntry e = history_get(history, i);
            int base = off + i * 4;
            out[base + 0] = (float)e.acting_player / 6.0f;
            out[base + 1] = (float)e.action / 32.0f;
            out[base + 2] = (float)e.phase / 10.0f;
            out[base + 3] = (float)e.result / 255.0f;
        }
    }
    /* off += 256; total = 407 */
}
