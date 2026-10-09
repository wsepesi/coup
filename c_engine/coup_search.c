/*
 * coup_search.c — opt-in search API over the C engine. See coup_search.h.
 *
 * Only reads/writes Game through the documented bit layout in coup_core.h
 * and the public engine API; nothing here is on the step hot path.
 */
#include "coup_search.h"

#include <stdlib.h>
#include <string.h>

/* ---- Engine-internal _pad fields (layout documented in coup_core.h) ---- */

#define LC_ADVANCE_TURN    0
#define LC_CLAIMANT_REDRAW 1
#define LC_BLOCK_STANDS    2
#define LC_BLOCK_FAILED    3

static inline int pad_lose_ctx(const Game *g) { return g->_pad & 0xF; }
static inline int pad_claimant(const Game *g) { return (g->_pad >> 7) & 0x7; }
static inline int pad_claimed(const Game *g)  { return (g->_pad >> 10) & 0x7; }
static inline int pad_refund(const Game *g)   { return (g->_pad >> 13) & 0x1; }

static inline uint16_t pad_make(int ctx, int claimant, int claimed, int refund) {
    return (uint16_t)((ctx & 0xF) | ((claimant & 0x7) << 7) |
                      ((claimed & 0x7) << 10) | ((refund & 1) << 13));
}

/* Role claimed by a main action or block (-1 if none). */
static int action_role(int a) {
    if (a == ACT_TAX || a == ACT_BLOCK_DUKE) return DUKE;
    if (a == ACT_EXCHANGE || a == ACT_BLOCK_AMBASSADOR) return AMBASSADOR;
    if (a == ACT_BLOCK_CAPTAIN) return CAPTAIN;
    if (a == ACT_BLOCK_CONTESSA) return CONTESSA;
    if (a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6) return CAPTAIN;
    if (a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6) return ASSASSIN;
    return -1;
}

static inline int slot_alive(const Game *g, int p, int k) {
    return k ? player_card1_alive(g, p) : player_card0_alive(g, p);
}
static inline int slot_type(const Game *g, int p, int k) {
    return k ? player_card1_type(g, p) : player_card0_type(g, p);
}

/* A slot is hidden (part of the private hand) if it holds a live, dealt
 * card that is not the placeholder of a pending redraw. Undealt slots are
 * dead during PHASE_DEAL, so "alive" already implies "dealt". */
static inline int slot_hidden(const Game *g, int p, int k) {
    if (!slot_alive(g, p, k)) return 0;
    return !(get_phase(g) == PHASE_CHANCE_REDRAW && p == get_active_player(g) &&
             get_exchange_card0(g) == k);
}

/* ===================================================================== */
/* 1. History log                                                        */
/* ===================================================================== */

int coup_event_make(const Game *g, int action, CoupLogEvent *e) {
    int phase = get_phase(g);
    e->phase = (uint8_t)phase;
    e->action = (uint8_t)action;
    e->info = 0;
    e->claimant = 0;
    e->role = 0;

    if (is_chance_node(g)) {
        if (phase != PHASE_DEAL && is_done(g)) return -1;
        if (action < 0 || action >= 5 || deck_count(g, action) == 0) return -1;
        switch (phase) {
        case PHASE_DEAL:
            e->actor = (uint8_t)(get_pending_action(g) / 2);
            break;
        case PHASE_CHANCE_REDRAW:
            e->actor = (uint8_t)get_active_player(g);
            e->info = (uint8_t)get_exchange_card0(g); /* slot being refilled */
            break;
        default: /* PHASE_CHANCE_EXCHANGE */
            e->actor = (uint8_t)get_turn_player(g);
            break;
        }
        return 0;
    }

    if ((unsigned)action >= 32u || !((get_valid_actions(g) >> action) & 1u))
        return -1;
    int actor = get_active_player(g);
    e->actor = (uint8_t)actor;
    if (phase == PHASE_LOSE_CARD) {
        e->info = (uint8_t)slot_type(g, actor, action - ACT_DISCARD_SLOT0);
    } else if (action == ACT_CHALLENGE) {
        int claimant, role;
        if (phase == PHASE_CHALLENGE_ACTION) {
            claimant = get_turn_player(g);
            role = action_role(get_pending_action(g));
        } else {
            claimant = get_blocker(g);
            role = get_block_card(g);
        }
        e->info = (uint8_t)player_has_card(g, claimant, role);
        e->claimant = (uint8_t)claimant;
        e->role = (uint8_t)role;
    } else if (phase == PHASE_EXCHANGE_DISCARD) {
        int slot = action - ACT_DISCARD_SLOT0;
        int cards[4] = {player_card0_type(g, actor), player_card1_type(g, actor),
                        get_exchange_card0(g), get_exchange_card1(g)};
        e->info = (uint8_t)cards[slot];
    }
    return 0;
}

void coup_log_reset(CoupLog *log) {
    log->len = 0;
}

int coup_log_record(CoupLog *log, const Game *before, int action) {
    if (log->len >= COUP_LOG_CAPACITY) return -1;
    if (coup_event_make(before, action, &log->ev[log->len]) != 0) return -1;
    log->len++;
    return 0;
}

int coup_step_logged(Game *g, CoupLog *log, int action) {
    CoupLogEvent e;
    if (coup_event_make(g, action, &e) != 0) return -1;
    if (log && log->len >= COUP_LOG_CAPACITY) return -1;
    int rc = is_chance_node(g) ? apply_chance(g, action)
                               : step_deterministic(g, action);
    if (rc != 0) return rc; /* unreachable: coup_event_make checked legality */
    if (log) log->ev[log->len++] = e;
    return 0;
}

/* The engine's sampler (coup_core.c sample_card); must stay identical so
 * coup_step_logged_rng reproduces step_with_rng bit for bit. */
static int sample_card_like_engine(const Game *g, Xoshiro256 *rng) {
    int total = deck_total(g);
    if (total == 0) return -1;
    int r = (int)xoshiro256_uniform(rng, (uint32_t)total);
    for (int i = 0; i < 5; i++) {
        r -= deck_count(g, i);
        if (r < 0) return i;
    }
    return -1;
}

int coup_step_logged_rng(Game *g, CoupLog *log, int action) {
    int rc = is_chance_node(g) ? -1 : coup_step_logged(g, log, action);
    while (is_chance_node(g) && !is_done(g)) {
        /* Unreachable in a real game (capacity is a proven bound). */
        if (log && log->len >= COUP_LOG_CAPACITY) return -1;
        int card = sample_card_like_engine(g, &g->rng);
        if (card < 0 || coup_step_logged(g, log, card) != 0) break;
    }
    return rc;
}

int coup_log_same_view(const CoupLogEvent *a, const CoupLogEvent *b, int len,
                       int player) {
    for (int i = 0; i < len; i++) {
        const CoupLogEvent *x = &a[i], *y = &b[i];
        if (x->phase != y->phase || x->actor != y->actor) return 0;
        int own = x->actor == player;
        switch (x->phase) {
        case PHASE_DEAL:
        case PHASE_CHANCE_EXCHANGE:
            if (own && x->action != y->action) return 0;
            break;
        case PHASE_CHANCE_REDRAW:
            if (x->info != y->info) return 0;
            if (own && x->action != y->action) return 0;
            break;
        case PHASE_EXCHANGE_DISCARD:
            if (own && x->action != y->action) return 0;
            break;
        case PHASE_LOSE_CARD:
            if (x->action != y->action || x->info != y->info) return 0;
            break;
        default:
            if (x->action != y->action) return 0;
            if (x->action == ACT_CHALLENGE &&
                (x->info != y->info || x->claimant != y->claimant ||
                 x->role != y->role))
                return 0;
            break;
        }
    }
    return 1;
}

/* ===================================================================== */
/* 2. Public state                                                       */
/* ===================================================================== */

void coup_public_from_game(const Game *g, CoupPublicState *pub) {
    memset(pub, 0, sizeof(*pub));
    int n = get_num_players(g);
    int phase = get_phase(g);
    int ctx = pad_lose_ctx(g);
    pub->num_players = (uint8_t)n;
    pub->phase = (uint8_t)phase;
    pub->turn_player = (uint8_t)get_turn_player(g);
    pub->active_player = (uint8_t)get_active_player(g);
    pub->pending_action = (uint8_t)get_pending_action(g);
    if (phase == PHASE_CHALLENGE_ACTION || phase == PHASE_BLOCK ||
        phase == PHASE_CHALLENGE_BLOCK)
        pub->responded_mask = (uint8_t)get_responded_mask(g);
    if (phase == PHASE_CHALLENGE_BLOCK ||
        (phase == PHASE_LOSE_CARD &&
         (ctx == LC_BLOCK_STANDS || ctx == LC_BLOCK_FAILED))) {
        pub->blocker = (uint8_t)get_blocker(g);
        pub->block_card = (uint8_t)get_block_card(g);
    }
    if (phase == PHASE_LOSE_CARD || phase == PHASE_CHANCE_REDRAW)
        pub->lose_ctx = (uint8_t)ctx;
    if (phase == PHASE_LOSE_CARD && ctx == LC_CLAIMANT_REDRAW) {
        pub->claimant = (uint8_t)pad_claimant(g);
        pub->claimed_card = (uint8_t)pad_claimed(g);
    }
    if (phase == PHASE_CHANCE_REDRAW)
        pub->redraw_slot = (uint8_t)get_exchange_card0(g);
    if (phase == PHASE_CHANCE_EXCHANGE)
        pub->exchange_draws = get_exchange_card0(g) != 7;
    if (phase == PHASE_EXCHANGE_DISCARD) {
        pub->exchange_draws = 2;
        pub->exchange_picks = get_first_discard(g) != FIRST_DISCARD_NONE;
    }
    pub->refund_on_challenge = (uint8_t)pad_refund(g);
    pub->deck_size = (uint8_t)deck_total(g);
    int dealt = phase == PHASE_DEAL ? get_pending_action(g) : 2 * n;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        pub->revealed[p][0] = pub->revealed[p][1] = COUP_CARD_NONE;
        if (p >= n) continue;
        pub->coins[p] = (uint8_t)player_coins(g, p);
        for (int k = 0; k < 2; k++) {
            if (slot_alive(g, p, k))
                pub->card_alive[p] |= (uint8_t)(1 << k);
            else if (2 * p + k < dealt)
                pub->revealed[p][k] = (uint8_t)slot_type(g, p, k);
        }
    }
    pub->turn_count = g->turn_count;
}

int coup_public_equal(const CoupPublicState *a, const CoupPublicState *b) {
    uint64_t x[2], y[2];
    coup_public_pack(a, x);
    coup_public_pack(b, y);
    return x[0] == y[0] && x[1] == y[1];
}

static void put_bits(uint64_t out[2], int *pos, uint64_t v, int bits) {
    v &= (bits == 64) ? ~0ull : ((1ull << bits) - 1);
    int w = *pos >> 6, o = *pos & 63;
    out[w] |= v << o;
    if (o + bits > 64) out[w + 1] |= v >> (64 - o);
    *pos += bits;
}

void coup_public_pack(const CoupPublicState *pub, uint64_t out[2]) {
    out[0] = out[1] = 0;
    int pos = 0;
    put_bits(out, &pos, pub->num_players, 3);
    put_bits(out, &pos, pub->phase, 4);
    put_bits(out, &pos, pub->turn_player, 3);
    put_bits(out, &pos, pub->active_player, 3);
    put_bits(out, &pos, pub->pending_action, 6);
    put_bits(out, &pos, pub->responded_mask, 6);
    put_bits(out, &pos, pub->blocker, 3);
    put_bits(out, &pos, pub->block_card, 3);
    put_bits(out, &pos, pub->lose_ctx, 4);
    put_bits(out, &pos, pub->claimant, 3);
    put_bits(out, &pos, pub->claimed_card, 3);
    put_bits(out, &pos, pub->redraw_slot, 1);
    put_bits(out, &pos, pub->exchange_draws, 2);
    put_bits(out, &pos, pub->exchange_picks, 1);
    put_bits(out, &pos, pub->refund_on_challenge, 1);
    put_bits(out, &pos, pub->turn_count, 8);       /* <= MAX_TURNS = 200 */
    for (int p = 0; p < MAX_PLAYERS; p++) {
        put_bits(out, &pos, pub->coins[p], 4);
        put_bits(out, &pos, pub->card_alive[p], 2);
        put_bits(out, &pos, pub->revealed[p][0], 3);
        put_bits(out, &pos, pub->revealed[p][1], 3);
    }
    /* pos == 126; deck_size is implied by the rest. */
}

static inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

uint64_t coup_public_key(const CoupPublicState *pub) {
    uint64_t w[2];
    coup_public_pack(pub, w);
    return mix64(w[0] ^ mix64(w[1] + 0x9e3779b97f4a7c15ull));
}

static inline int pub_slot_hidden(const CoupPublicState *pub, int p, int k) {
    if (!((pub->card_alive[p] >> k) & 1)) return 0;
    return !(pub->phase == PHASE_CHANCE_REDRAW && p == pub->active_player &&
             pub->redraw_slot == k);
}

int coup_public_hand_size(const CoupPublicState *pub, int p) {
    if (p < 0 || p >= pub->num_players) return 0;
    return pub_slot_hidden(pub, p, 0) + pub_slot_hidden(pub, p, 1);
}

void coup_public_unseen(const CoupPublicState *pub, int unseen[5]) {
    for (int c = 0; c < 5; c++) unseen[c] = 3;
    for (int p = 0; p < pub->num_players; p++)
        for (int k = 0; k < 2; k++)
            if (pub->revealed[p][k] < 5) unseen[pub->revealed[p][k]]--;
}

/* Any consistent hidden assignment (greedy), for queries that do not depend
 * on hidden cards. Returns 0 / -1. */
static int dummy_assignment(const CoupPublicState *pub,
                            uint8_t hands[MAX_PLAYERS][2],
                            CoupExchangeHidden *xh) {
    int left[5];
    coup_public_unseen(pub, left);
    int c = 0;
    memset(hands, COUP_CARD_NONE, sizeof(uint8_t) * MAX_PLAYERS * 2);
    for (int p = 0; p < pub->num_players; p++) {
        for (int k = 0; k < 2; k++) {
            if (!pub_slot_hidden(pub, p, k)) continue;
            while (c < 5 && left[c] == 0) c++;
            if (c == 5) return -1;
            hands[p][k] = (uint8_t)c;
            left[c]--;
        }
    }
    xh->draw[0] = xh->draw[1] = COUP_CARD_NONE;
    xh->first_discard = FIRST_DISCARD_NONE;
    for (int d = 0; d < pub->exchange_draws; d++) {
        while (c < 5 && left[c] == 0) c++;
        if (c == 5) return -1;
        xh->draw[d] = (uint8_t)c;
        left[c]--;
    }
    if (pub->exchange_picks) xh->first_discard = 2; /* always a legal first pick */
    return 0;
}

uint32_t coup_public_valid_actions(const CoupPublicState *pub) {
    /* The one mask that is NOT public: the exchanger's second pick depends
     * on their (private) first pick. */
    if (pub->phase == PHASE_EXCHANGE_DISCARD && pub->exchange_picks) return 0;
    uint8_t hands[MAX_PLAYERS][2];
    CoupExchangeHidden xh;
    Game g;
    if (dummy_assignment(pub, hands, &xh) != 0) return 0;
    if (coup_game_from_public(pub, hands, &xh, &g) != 0) return 0;
    return get_valid_actions(&g);
}

int coup_public_is_chance(const CoupPublicState *pub) {
    return pub->phase == PHASE_DEAL || pub->phase == PHASE_CHANCE_REDRAW ||
           pub->phase == PHASE_CHANCE_EXCHANGE;
}

int coup_public_is_terminal(const CoupPublicState *pub) {
    if (pub->phase == PHASE_DEAL) return 0;
    if (pub->turn_count >= MAX_TURNS) return 1;
    int alive = 0;
    for (int p = 0; p < pub->num_players; p++) alive += pub->card_alive[p] != 0;
    return alive <= 1;
}

/* ===================================================================== */
/* 3. Private hands                                                      */
/* ===================================================================== */

/* Two-card hands in index order. */
static const uint8_t kHand2[COUP_HANDS_2][2] = {
    {0,0},{0,1},{0,2},{0,3},{0,4},{1,1},{1,2},{1,3},{1,4},
    {2,2},{2,3},{2,4},{3,3},{3,4},{4,4},
};

static inline int hand2_index(int a, int b) {
    if (a > b) { int t = a; a = b; b = t; }
    return a * 5 - a * (a - 1) / 2 + (b - a);
}

int coup_hand_count(int size) {
    return size == 2 ? COUP_HANDS_2 : size == 1 ? COUP_HANDS_1 : size == 0 ? 1 : 0;
}

int coup_hand_index(int size, const int cards[]) {
    switch (size) {
    case 0: return 0;
    case 1: return (unsigned)cards[0] < 5u ? cards[0] : -1;
    case 2:
        if ((unsigned)cards[0] >= 5u || (unsigned)cards[1] >= 5u) return -1;
        return hand2_index(cards[0], cards[1]);
    default: return -1;
    }
}

int coup_hand_cards(int size, int index, int cards_out[2]) {
    if (index < 0 || index >= coup_hand_count(size)) return -1;
    if (size == 2) {
        cards_out[0] = kHand2[index][0];
        cards_out[1] = kHand2[index][1];
    } else if (size == 1) {
        cards_out[0] = index;
    }
    return 0;
}

int coup_hand_multiplicity(int size, int index, int card) {
    int c[2];
    if (coup_hand_cards(size, index, c) != 0) return 0;
    int m = 0;
    for (int i = 0; i < size; i++) m += c[i] == card;
    return m;
}

int coup_hand_remove(int size, int index, int card) {
    int c[2];
    if (size < 1 || coup_hand_cards(size, index, c) != 0) return -1;
    if (size == 1) return c[0] == card ? 0 : -1;
    if (c[0] == card) return c[1];
    if (c[1] == card) return c[0];
    return -1;
}

int coup_hand_add(int size, int index, int card) {
    int c[2];
    if ((unsigned)card >= 5u || size >= 2 || coup_hand_cards(size, index, c) != 0)
        return -1;
    if (size == 0) return card;
    return hand2_index(c[0], card);
}

int coup_hand_challenge_consistent(int size, int index, int role,
                                   int claim_true) {
    if (index < 0 || index >= coup_hand_count(size)) return 0;
    int has = coup_hand_multiplicity(size, index, role) > 0;
    return claim_true ? has : !has;
}

int coup_game_hand(const Game *g, int p, int cards_out[2], int *index_out) {
    int n = 0;
    for (int k = 0; k < 2; k++)
        if (slot_hidden(g, p, k)) cards_out[n++] = slot_type(g, p, k);
    if (index_out) *index_out = coup_hand_index(n, cards_out);
    return n;
}

int coup_hand_slot_of(const Game *g, int p, int type) {
    for (int k = 0; k < 2; k++)
        if (slot_hidden(g, p, k) && slot_type(g, p, k) == type) return k;
    return -1;
}

int coup_canonicalize_hands(Game *g) {
    int swapped = 0;
    int n = get_num_players(g);
    for (int p = 0; p < n; p++) {
        if (!slot_hidden(g, p, 0) || !slot_hidden(g, p, 1)) continue;
        /* Mid-exchange after a first pick on a hand slot: picks are an
         * ordered pair (first < second), so relabeling the hand slots would
         * change which discard sets remain reachable. Leave it alone. */
        if (get_phase(g) == PHASE_EXCHANGE_DISCARD && p == get_turn_player(g) &&
            get_first_discard(g) < 2)
            continue;
        int t0 = player_card0_type(g, p), t1 = player_card1_type(g, p);
        if (t0 <= t1) continue;
        set_player_card0_type(g, p, t1);
        set_player_card1_type(g, p, t0);
        swapped |= 1 << p;
    }
    return swapped;
}

void coup_hidden_from_game(const Game *g, uint8_t hands[MAX_PLAYERS][2],
                           CoupExchangeHidden *xh) {
    int n = get_num_players(g);
    for (int p = 0; p < MAX_PLAYERS; p++)
        for (int k = 0; k < 2; k++)
            hands[p][k] = (p < n && slot_hidden(g, p, k))
                ? (uint8_t)slot_type(g, p, k) : COUP_CARD_NONE;
    xh->draw[0] = xh->draw[1] = COUP_CARD_NONE;
    xh->first_discard = FIRST_DISCARD_NONE;
    int phase = get_phase(g);
    if (phase == PHASE_CHANCE_EXCHANGE && get_exchange_card0(g) != 7) {
        xh->draw[0] = (uint8_t)get_exchange_card0(g);
    } else if (phase == PHASE_EXCHANGE_DISCARD) {
        xh->draw[0] = (uint8_t)get_exchange_card0(g);
        xh->draw[1] = (uint8_t)get_exchange_card1(g);
        xh->first_discard = (uint8_t)get_first_discard(g);
    }
}

int coup_deck_from_hidden(const CoupPublicState *pub,
                          const uint8_t hands[MAX_PLAYERS][2],
                          const CoupExchangeHidden *xh, int deck_out[5]) {
    int d[5] = {3, 3, 3, 3, 3};
    int n = pub->num_players;
    if (n < 2 || n > MAX_PLAYERS) return -1;
    for (int p = 0; p < n; p++) {
        for (int k = 0; k < 2; k++) {
            int t;
            if (pub_slot_hidden(pub, p, k)) t = hands[p][k];
            else if (pub->revealed[p][k] != COUP_CARD_NONE) t = pub->revealed[p][k];
            else continue;
            if (t >= 5) return -1;
            d[t]--;
        }
    }
    if (pub->exchange_draws) {
        if (!xh) return -1;
        for (int i = 0; i < pub->exchange_draws; i++) {
            if (xh->draw[i] >= 5) return -1;
            d[xh->draw[i]]--;
        }
    }
    for (int c = 0; c < 5; c++) {
        if (d[c] < 0) return -1;
        deck_out[c] = d[c];
    }
    return 0;
}

/* ===================================================================== */
/* 4. Construct a Game                                                   */
/* ===================================================================== */

int coup_game_from_public(const CoupPublicState *pub,
                          const uint8_t hands[MAX_PLAYERS][2],
                          const CoupExchangeHidden *xh, Game *out) {
    int n = pub->num_players;
    int phase = pub->phase;
    if (n < 2 || n > MAX_PLAYERS || phase > PHASE_EXCHANGE_DISCARD ||
        phase == PHASE_RESOLVE)
        return -1;
    if (pub->turn_player >= n || pub->active_player >= n) return -1;
    int deck[5];
    if (coup_deck_from_hidden(pub, hands, xh, deck) != 0) return -1;

    Game g;
    memset(&g, 0, sizeof(g));
    set_num_players(&g, n);
    for (int c = 0; c < 5; c++) deck_set_count(&g, c, deck[c]);
    for (int p = 0; p < n; p++) {
        set_player_coins(&g, p, pub->coins[p]);
        for (int k = 0; k < 2; k++) {
            int alive = (pub->card_alive[p] >> k) & 1;
            int type = 0; /* undealt slot or redraw placeholder */
            if (pub_slot_hidden(pub, p, k)) type = hands[p][k];
            else if (pub->revealed[p][k] != COUP_CARD_NONE) type = pub->revealed[p][k];
            if (k) { set_player_card1_type(&g, p, type); set_player_card1_alive(&g, p, alive); }
            else   { set_player_card0_type(&g, p, type); set_player_card0_alive(&g, p, alive); }
        }
    }
    set_phase(&g, phase);
    set_turn_player(&g, pub->turn_player);
    set_active_player(&g, pub->active_player);
    set_pending_action(&g, pub->pending_action);

    /* Fields the public state normalizes: restore what the engine holds. */
    int dead = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (p >= n || !pub->card_alive[p]) dead |= 1 << p;
    set_responded_mask(&g, phase == PHASE_MAIN_ACTION ? dead : pub->responded_mask);
    set_exchange_card0(&g, 0);
    set_exchange_card1(&g, 0);
    set_first_discard(&g, FIRST_DISCARD_NONE);
    if (phase == PHASE_CHANCE_REDRAW) {
        set_exchange_card0(&g, pub->redraw_slot);
    } else if (phase == PHASE_CHANCE_EXCHANGE) {
        set_exchange_card0(&g, pub->exchange_draws ? xh->draw[0] : 7);
        set_exchange_card1(&g, 7);
    } else if (phase == PHASE_EXCHANGE_DISCARD) {
        set_exchange_card0(&g, xh->draw[0]);
        set_exchange_card1(&g, xh->draw[1]);
        if (pub->exchange_picks) {
            int fd = xh->first_discard;
            int tp = pub->turn_player;
            int ok = fd == 2 || (fd < 2 && ((pub->card_alive[tp] >> fd) & 1));
            if (!ok) return -1;
            set_first_discard(&g, fd);
        }
    }
    set_blocker(&g, pub->blocker);
    set_block_card(&g, pub->block_card);
    g._pad = pad_make(pub->lose_ctx, pub->claimant, pub->claimed_card,
                      pub->refund_on_challenge);
    g.turn_count = pub->turn_count;
    xoshiro256_seed(&g.rng, 0);
    *out = g;
    return 0;
}

int coup_game_from_public_idx(const CoupPublicState *pub,
                              const int hand_index[MAX_PLAYERS],
                              const CoupExchangeHidden *xh, Game *out) {
    uint8_t hands[MAX_PLAYERS][2];
    memset(hands, COUP_CARD_NONE, sizeof(hands));
    for (int p = 0; p < pub->num_players && p < MAX_PLAYERS; p++) {
        int size = coup_public_hand_size(pub, p);
        int cards[2];
        if (coup_hand_cards(size, hand_index[p], cards) != 0) return -1;
        int i = 0;
        for (int k = 0; k < 2; k++)
            if (pub_slot_hidden(pub, p, k)) hands[p][k] = (uint8_t)cards[i++];
    }
    return coup_game_from_public(pub, hands, xh, out);
}

/* ===================================================================== */
/* 6. Resampling                                                         */
/* ===================================================================== */
/*
 * Samples a complete history that `player` cannot tell apart from the real
 * one: same public actions, same own cards and draws, and every public
 * revelation (lost cards, challenge outcomes, redraw slots) reproduced
 * exactly when the history is replayed through the engine.
 *
 * Model: the 15 cards are tokens moving between deck, hands and exchange
 * draws. Unobserved choices are resampled: opponents' exchange discard picks
 * uniformly over legal picks (OpenSpiel's convention for unobserved actions),
 * chance draws from the deck. Observations become constraints on a token's
 * role: own draws and revealed cards fix it, a failed claim excludes the role
 * for the claimant's live cards, a proven claim fixes the redraw slot (and
 * excludes the role from slot 0 when the engine picked slot 1).
 *
 * Two passes per attempt:
 *   1. Positions only. With the opponents' discard picks drawn up front, each
 *      chance draw starts a "hand life" (the card's stay in a hand until it
 *      returns to the deck) and every constraint lands on exactly one life.
 *   2. Token flow. Each draw picks a deck token weighted by how likely it is
 *      to satisfy its life's constraints (fixed tokens: 0/1; unfixed tokens:
 *      share of still-unplaced copies of the allowed roles), then applies
 *      them. Unfixed tokens finally get roles drawn uniformly from the
 *      remaining multiset subject to exclusions (exact DP over role counts,
 *      over the mixed-radix box of remaining counts only).
 * Local repairs handle most dead ends: pass 1 re-picks one recent exchange
 * of the seat whose observation failed; pass 2 swaps a needed token out of an
 * unconstrained hand for one that was in the deck all along, and (new in the
 * C port) falls back to relabeling two tokens over a suffix of the history
 * (relabel_repair), which keeps very long exchange-heavy histories
 * resamplable. An attempt that still dead-ends is discarded and retried
 * (~1.2 attempts on average on random-play games). The
 * result is always a valid, consistent history; its distribution approximates
 * (does not exactly equal) the chance-weighted posterior, and matches it
 * exactly right after the deal.
 */

#define NTOK 15

typedef struct {
    int8_t eq;    /* fixed role, or -1 */
    uint8_t neq;  /* bitmask of excluded roles */
} Constraint;

static inline int c_fix(Constraint *c, int role) {
    if (c->eq >= 0) return c->eq == role;
    if (c->neq & (1 << role)) return 0;
    c->eq = (int8_t)role;
    return 1;
}
static inline int c_exclude(Constraint *c, int role) {
    if (c->eq >= 0) return c->eq != role;
    c->neq |= (uint8_t)(1 << role);
    return 1;
}
static inline int c_allows(const Constraint *c, int role) {
    return c->eq >= 0 ? c->eq == role : !(c->neq & (1 << role));
}

typedef struct {
    const CoupLogEvent *ev;
    int len, player;
    CoupUniformFn uniform;
    void *ctx;
    Constraint life[COUP_LOG_CAPACITY]; /* per draw event: its hand life */
    int8_t picks[COUP_LOG_CAPACITY];    /* resampled opponent discard slots */
    int8_t token_at[COUP_LOG_CAPACITY]; /* pass 2: token drawn at event */
    int16_t life_end[COUP_LOG_CAPACITY];/* pass 2: event the life's token
                                           went back to the deck, or -1 */
    int cur;                            /* pass 2: draw event being filled */
    /* relabel_repair scratch: lives grouped by token, prefix/suffix merges */
    int16_t tl[COUP_LOG_CAPACITY];
    Constraint pre[COUP_LOG_CAPACITY + NTOK], suf[COUP_LOG_CAPACITY + NTOK];
    uint8_t pre_ok[COUP_LOG_CAPACITY + NTOK], suf_ok[COUP_LOG_CAPACITY + NTOK];
    int fail_seat, fail_event;          /* pass 1 failure */
    Constraint tokens[NTOK];
    int fixed[5];                       /* tokens fixed to each role */
    int unfixed;
    int holder[NTOK];                   /* hand life holding it, or -1 */
    int deck_since[NTOK];               /* event it last entered the deck */
    int deck[NTOK], deck_n;
} Attempt;

static inline int rand_below(Attempt *a, int n) {
    int k = (int)(a->uniform(a->ctx) * n);
    return k < n ? k : n - 1;
}

static inline int fail_at(Attempt *a, int seat, int event) {
    a->fail_seat = seat;
    a->fail_event = event;
    return 0;
}

static int draw_for(Attempt *a, const Constraint *life);
static int swap_repair(Attempt *a, const Constraint *life);
static int relabel_repair(Attempt *a, const Constraint *life);

/* Walk hooks. plan: pass 1 (own draws fix their life). Otherwise pass 2. */
static int on_draw(Attempt *a, int i, int own, int plan) {
    if (plan) return !own || c_fix(&a->life[i], a->ev[i].action);
    a->cur = i;
    int t = draw_for(a, &a->life[i]);
    /* The C++ original allowed 4 swap repairs; extra rounds only matter
     * where it would have dead-ended. */
    for (int r = 0; t < 0 && r < 16; r++) {
        if (!swap_repair(a, &a->life[i]) && !relabel_repair(a, &a->life[i]))
            break;
        t = draw_for(a, &a->life[i]);
    }
    if (t < 0) return 0;
    a->token_at[i] = (int8_t)t;
    a->life_end[i] = -1;
    a->holder[t] = i;
    return 1;
}

static void on_return(Attempt *a, int life, int i, int plan) {
    if (plan) return;
    int t = a->token_at[life];
    a->deck[a->deck_n++] = t;
    a->holder[t] = -1;
    a->deck_since[t] = i;
    a->life_end[life] = (int16_t)i;
}

/* Walks the public view of the history, moving "hand lives" (indexed by the
 * draw event that started them) around with the same slot rules as the
 * engine. With plan, also draws the opponents' discard picks and records
 * constraints on lives. Returns 1 ok, 0 dead end, -1 malformed log. */
static int walk(Attempt *a, int plan) {
    a->fail_seat = -1;
    a->fail_event = a->len;
    int hand[MAX_PLAYERS][2];
    uint8_t alive[MAX_PLAYERS][2];
    memset(alive, 0, sizeof(alive));
    for (int p = 0; p < MAX_PLAYERS; p++) hand[p][0] = hand[p][1] = -1;
    int xch[2] = {-1, -1};
    int xch_drawn = 0, dealt = 0, first = -1;
    int proof_claimant = -1, proof_role = -1;
    for (int i = 0; i < a->len; i++) {
        const CoupLogEvent *e = &a->ev[i];
        if (e->actor >= MAX_PLAYERS) return -1;
        const int own = e->actor == a->player;
        int *h = hand[e->actor];
        switch (e->phase) {
        case PHASE_DEAL: {
            const int k = dealt++ % 2;
            h[k] = i;
            alive[e->actor][k] = 1;
            if (!on_draw(a, i, own, plan)) return 0;
            break;
        }
        case PHASE_CHANCE_REDRAW: {
            /* begin_redraw: the proven card (slot e->info) returns to the
             * deck; the engine prefers slot 0 when it holds the role. */
            const int k = e->info;
            if (proof_claimant != e->actor || k > 1 || h[k] < 0) return -1;
            if (plan && (!c_fix(&a->life[h[k]], proof_role) ||
                         (k == 1 && alive[e->actor][0] &&
                          !c_exclude(&a->life[h[0]], proof_role))))
                return fail_at(a, e->actor, i);
            on_return(a, h[k], i, plan);
            proof_claimant = -1;
            h[k] = i;
            if (!on_draw(a, i, own, plan)) return 0;
            break;
        }
        case PHASE_CHANCE_EXCHANGE:
            xch[xch_drawn++ % 2] = i;
            if (!on_draw(a, i, own, plan)) return 0;
            break;
        case PHASE_LOSE_CARD: {
            const int k = e->action - ACT_DISCARD_SLOT0;
            if (k < 0 || k > 1 || h[k] < 0) return -1;
            if (plan && !c_fix(&a->life[h[k]], e->info))
                return fail_at(a, e->actor, i);
            alive[e->actor][k] = 0;
            break;
        }
        case PHASE_EXCHANGE_DISCARD: {
            if (!own && plan && a->picks[i] < 0) {
                /* Same legality rule as get_valid_actions. */
                unsigned avail = (alive[e->actor][0] ? 1u : 0u) |
                                 (alive[e->actor][1] ? 2u : 0u) | 0xCu;
                avail &= first < 0 ? 0x7u : ~((2u << first) - 1);
                int opts[4], n = 0;
                for (int b = 0; b < 4; b++)
                    if (avail & (1u << b)) opts[n++] = b;
                if (n == 0) return -1;
                a->picks[i] = (int8_t)opts[rand_below(a, n)];
            }
            const int slot = own ? e->action - ACT_DISCARD_SLOT0 : a->picks[i];
            if (slot < 0 || slot > 3) return -1;
            if (first < 0) {
                first = slot;
                break;
            }
            /* Second pick: mirror step_deterministic's refill. */
            const int cards[4] = {h[0], h[1], xch[0], xch[1]};
            unsigned keep = 0xFu & ~(1u << first) & ~(1u << slot);
            if (!alive[e->actor][0]) keep &= ~1u;
            if (!alive[e->actor][1]) keep &= ~2u;
            if (cards[first] < 0 || cards[slot] < 0) return -1;
            on_return(a, cards[first], i, plan);
            on_return(a, cards[slot], i, plan);
            if (alive[e->actor][0]) {
                h[0] = cards[__builtin_ctz(keep)];
                keep &= keep - 1;
            }
            if (alive[e->actor][1]) h[1] = cards[__builtin_ctz(keep)];
            first = -1;
            break;
        }
        default:
            if (e->action == ACT_CHALLENGE) {
                if (e->claimant >= MAX_PLAYERS || e->role >= 5) return -1;
                if (e->info) {
                    proof_claimant = e->claimant;
                    proof_role = e->role;
                } else if (plan) {
                    for (int k = 0; k < 2; k++) {
                        if (alive[e->claimant][k] &&
                            !c_exclude(&a->life[hand[e->claimant][k]], e->role))
                            return fail_at(a, e->claimant, i);
                    }
                }
            }
            break;
        }
    }
    /* A proven claim whose redraw has not happened yet (the challenger is
     * still choosing a card to lose): some live slot holds the role. */
    if (plan && proof_claimant >= 0) {
        int opts[2], n = 0;
        for (int k = 0; k < 2; k++) {
            if (alive[proof_claimant][k] &&
                c_allows(&a->life[hand[proof_claimant][k]], proof_role))
                opts[n++] = k;
        }
        if (n == 0) return fail_at(a, proof_claimant, a->len);
        c_fix(&a->life[hand[proof_claimant][opts[rand_below(a, n)]]], proof_role);
    }
    return 1;
}

static void reset_lives(Attempt *a) {
    for (int i = 0; i < a->len; i++) {
        a->life[i].eq = -1;
        a->life[i].neq = 0;
    }
}

/* Pass 1: draw the opponents' discard picks and collect each hand life's
 * constraints. When the picks contradict a later observation of seat q,
 * only one of q's two latest exchanges before it is re-picked (local repair)
 * rather than starting over. Returns 1 / 0 / -1 (malformed). */
static int plan_lives(Attempt *a) {
    for (int tries = 0; tries < 64; tries++) {
        reset_lives(a);
        int rc = walk(a, 1);
        if (rc != 0) return rc;
        if (a->fail_seat < 0 || a->fail_seat == a->player) return 0;
        /* Second-pick events of fail_seat's exchanges before the failure. */
        int ex[2], n = 0;
        for (int i = a->fail_event - 1; i >= 0 && n < 2; i--) {
            const CoupLogEvent *e = &a->ev[i];
            if (e->phase == PHASE_EXCHANGE_DISCARD && e->actor == a->fail_seat &&
                i > 0 && a->ev[i - 1].phase == PHASE_EXCHANGE_DISCARD) {
                ex[n++] = i;
                i--; /* skip the first pick */
            }
        }
        if (n == 0) return 0;
        const int second = ex[rand_below(a, n)];
        a->picks[second] = a->picks[second - 1] = -1;
    }
    return 0;
}

/* How likely deck token tok is to satisfy hand life `life`: 0/1 for a
 * fixed token, else the share of still-unplaced copies of roles both allow. */
static double weight(const Attempt *a, const Constraint *life,
                     const Constraint *tok) {
    if (tok->eq >= 0) return c_allows(life, tok->eq) ? 1.0 : 0.0;
    if (a->unfixed == 0) return 0.0;
    int copies = 0;
    for (int c = 0; c < 5; c++)
        if (c_allows(life, c) && c_allows(tok, c)) copies += 3 - a->fixed[c];
    return (double)copies / a->unfixed;
}

/* No deck token can satisfy `life`. Find a token X that could, held by an
 * opponent's unconstrained hand life L, and a deck token Y that has been in
 * the deck since before L drew X and suits L: then L could equally have
 * drawn Y, which puts X back in the deck. Swaps one random such pair. */
static int swap_repair(Attempt *a, const Constraint *life) {
    uint8_t px[NTOK * NTOK], pj[NTOK * NTOK];
    int np = 0;
    for (int x = 0; x < NTOK; x++) {
        const int l = a->holder[x];
        if (l < 0 || a->life[l].eq >= 0 || weight(a, life, &a->tokens[x]) <= 0)
            continue;
        for (int j = 0; j < a->deck_n; j++) {
            const int y = a->deck[j];
            if (a->deck_since[y] <= l && weight(a, &a->life[l], &a->tokens[y]) > 0) {
                px[np] = (uint8_t)x;
                pj[np] = (uint8_t)j;
                np++;
            }
        }
    }
    if (np == 0) return 0;
    const int pick = rand_below(a, np);
    const int x = px[pick], j = pj[pick];
    const int l = a->holder[x];
    const int y = a->deck[j];
    for (int c = 0; c < 5; c++)
        if ((a->life[l].neq >> c) & 1) c_exclude(&a->tokens[y], c);
    a->deck[j] = x;
    a->deck_since[x] = l;
    a->holder[x] = -1;
    a->holder[y] = l;
    a->token_at[l] = (int8_t)y;
    return 1;
}

/* dst &= src; 0 if they conflict. */
static int c_merge(Constraint *dst, const Constraint *src) {
    if (src->eq >= 0 && !c_fix(dst, src->eq)) return 0;
    for (int c = 0; c < 5; c++)
        if (((src->neq >> c) & 1) && !c_exclude(dst, c)) return 0;
    return 1;
}

/* Fallback when swap_repair finds nothing (long, exchange-heavy histories
 * where most tokens are fixed and the deck is small). Not part of the
 * original C++ algorithm; only reached where that one dead-ended.
 *
 * If tokens X and Y were both in the deck at some time T, swapping their
 * identities in every hand life that starts at or after T is again a valid
 * token flow. With X held now and Y in the deck now, the swap puts X back in
 * the deck. It is valid when the merged constraints of both relabeled
 * tokens stay consistent (and no role gets more than 3 fixed tokens). Picks
 * one random valid (X, Y, T) by reservoir sampling. Only the per-token life
 * boundaries matter, so T is enumerated as overlapping deck intervals of X
 * and Y (two-pointer merge over each token's lives). */
static int relabel_repair(Attempt *a, const Constraint *life) {
    const int cur = a->cur;
    /* Lives (draw events before cur) grouped by token, in time order. */
    int off[NTOK + 1], cnt[NTOK];
    memset(cnt, 0, sizeof(cnt));
    for (int i = 0; i < cur; i++)
        if (coup_event_is_chance(&a->ev[i])) cnt[a->token_at[i]]++;
    off[0] = 0;
    for (int t = 0; t < NTOK; t++) off[t + 1] = off[t] + cnt[t];
    int fill[NTOK];
    memcpy(fill, off, sizeof(fill));
    for (int i = 0; i < cur; i++)
        if (coup_event_is_chance(&a->ev[i])) a->tl[fill[a->token_at[i]]++] = (int16_t)i;
    /* pre[off[t] + t + k]: merge of t's first k lives; suf[...]: of lives
     * k.. (k = 0..cnt[t]); ok flags mark consistent merges. */
    for (int t = 0; t < NTOK; t++) {
        const int base = off[t] + t, n = cnt[t];
        Constraint c = {-1, 0};
        uint8_t ok = 1;
        a->pre[base] = c;
        a->pre_ok[base] = 1;
        for (int k = 0; k < n; k++) {
            ok &= c_merge(&c, &a->life[a->tl[off[t] + k]]);
            a->pre[base + k + 1] = c;
            a->pre_ok[base + k + 1] = ok;
        }
        c.eq = -1;
        c.neq = 0;
        ok = 1;
        a->suf[base + n] = c;
        a->suf_ok[base + n] = 1;
        for (int k = n - 1; k >= 0; k--) {
            ok &= c_merge(&c, &a->life[a->tl[off[t] + k]]);
            a->suf[base + k] = c;
            a->suf_ok[base + k] = ok;
        }
    }
    int found = 0, bx = -1, by = -1, bkx = 0, bky = 0;
    Constraint bnx = {-1, 0}, bny = {-1, 0};
    for (int x = 0; x < NTOK; x++) {
        if (a->holder[x] < 0) continue;
        for (int j = 0; j < a->deck_n; j++) {
            const int y = a->deck[j];
            /* Deck interval k of token t, k = 0..cnt: [end of life k-1
             * (0 if k == 0), start of life k (cur if k == cnt)]. X must
             * leave before its current life (k < cnt[x]). */
            int kx = 0, ky = 0;
            while (kx < cnt[x] && ky <= cnt[y]) {
                const int ax = kx ? a->life_end[a->tl[off[x] + kx - 1]] : 0;
                const int bxx = a->tl[off[x] + kx];
                const int ay = ky ? a->life_end[a->tl[off[y] + ky - 1]] : 0;
                const int byy = ky < cnt[y] ? a->tl[off[y] + ky] : cur;
                const int lo = ax > ay ? ax : ay, hi = bxx < byy ? bxx : byy;
                if (ax >= 0 && ay >= 0 && lo <= hi) {
                    const int px = off[x] + x, py = off[y] + y;
                    Constraint nx = a->pre[px + kx], ny = a->pre[py + ky];
                    if (a->pre_ok[px + kx] && a->pre_ok[py + ky] &&
                        a->suf_ok[py + ky] && a->suf_ok[px + kx] &&
                        c_merge(&nx, &a->suf[py + ky]) &&
                        c_merge(&ny, &a->suf[px + kx])) {
                        int useful = 0;
                        for (int c = 0; c < 5; c++)
                            useful |= c_allows(life, c) && c_allows(&nx, c);
                        int fixed[5];
                        memcpy(fixed, a->fixed, sizeof(fixed));
                        if (a->tokens[x].eq >= 0) fixed[a->tokens[x].eq]--;
                        if (a->tokens[y].eq >= 0) fixed[a->tokens[y].eq]--;
                        if (nx.eq >= 0) fixed[nx.eq]++;
                        if (ny.eq >= 0) fixed[ny.eq]++;
                        int over = 0;
                        for (int c = 0; c < 5; c++) over |= fixed[c] > 3;
                        if (useful && !over && rand_below(a, ++found) == 0) {
                            bx = x; by = y; bkx = kx; bky = ky;
                            bnx = nx; bny = ny;
                        }
                    }
                }
                /* Advance whichever interval ends first. */
                if (bxx < byy) kx++;
                else ky++;
            }
        }
    }
    if (!found) return 0;
    const int x = bx, y = by;
    const int L = a->holder[x];
    for (int k = bkx; k < cnt[x]; k++) a->token_at[a->tl[off[x] + k]] = (int8_t)y;
    for (int k = bky; k < cnt[y]; k++) a->token_at[a->tl[off[y] + k]] = (int8_t)x;
    for (int j = 0; j < a->deck_n; j++)
        if (a->deck[j] == y) a->deck[j] = x;
    if (bky < cnt[y])
        a->deck_since[x] = a->deck_since[y];
    else
        a->deck_since[x] = bkx ? a->life_end[a->tl[off[x] + bkx - 1]] : 0;
    a->holder[x] = -1;
    a->holder[y] = L;
    a->tokens[x] = bnx;
    a->tokens[y] = bny;
    memset(a->fixed, 0, sizeof(a->fixed));
    a->unfixed = 0;
    for (int t = 0; t < NTOK; t++) {
        if (a->tokens[t].eq >= 0) a->fixed[a->tokens[t].eq]++;
        else a->unfixed++;
    }
    return 1;
}

/* Picks a deck token for a hand life and applies the life's constraints. */
static int draw_for(Attempt *a, const Constraint *life) {
    const int n = a->deck_n;
    double w[NTOK], total = 0;
    for (int i = 0; i < n; i++) {
        w[i] = weight(a, life, &a->tokens[a->deck[i]]);
        total += w[i];
    }
    if (total <= 0) return -1;
    double u = a->uniform(a->ctx) * total;
    int idx = -1;
    for (int i = 0; i < n && idx < 0; i++)
        if (w[i] > 0 && (u -= w[i]) < 0) idx = i;
    for (int i = n - 1; i >= 0 && idx < 0; i--) /* rounding: last candidate */
        if (w[i] > 0) idx = i;
    const int t = a->deck[idx];
    a->deck[idx] = a->deck[--a->deck_n];
    Constraint *tok = &a->tokens[t];
    if (life->eq >= 0 && tok->eq < 0) {
        if (a->fixed[life->eq] == 3 || !c_fix(tok, life->eq)) return -1;
        a->fixed[life->eq]++;
        a->unfixed--;
    }
    for (int c = 0; c < 5; c++)
        if ((life->neq >> c) & 1) c_exclude(tok, c); /* compatible by weight > 0 */
    return t;
}

/* Pass 2: assign a deck token to every hand life. */
static int flow_tokens(Attempt *a) {
    a->deck_n = 0;
    for (int t = 0; t < NTOK; t++) {
        a->deck[a->deck_n++] = t;
        a->holder[t] = -1;
        a->deck_since[t] = 0;
        a->tokens[t].eq = -1;
        a->tokens[t].neq = 0;
    }
    memset(a->fixed, 0, sizeof(a->fixed));
    a->unfixed = NTOK;
    return walk(a, 0);
}

/* Unfixed tokens get a uniformly random arrangement of the remaining roles
 * that respects their exclusions. ways[st] counts completions for the
 * remaining-count vector st (mixed radix over 0..remaining[c]); the token
 * being assigned at st is unfixed[m - sum(st)]. */
static int assign_roles(Attempt *a, int roles_out[NTOK]) {
    int remaining[5] = {3, 3, 3, 3, 3};
    int unfixed[NTOK], m = 0;
    for (int t = 0; t < NTOK; t++) {
        if (a->tokens[t].eq >= 0) {
            if (--remaining[a->tokens[t].eq] < 0) return 0;
            roles_out[t] = a->tokens[t].eq;
        } else {
            unfixed[m++] = t;
        }
    }
    int stride[5], S = 1;
    for (int c = 0; c < 5; c++) {
        stride[c] = S;
        S *= remaining[c] + 1;
    }
    double ways[1024];
    int d[5] = {0, 0, 0, 0, 0}, sum = 0;
    for (int st = 0; st < S; st++) {
        if (st > 0) { /* odometer increment of d */
            for (int c = 0; c < 5; c++) {
                if (d[c] < remaining[c]) { d[c]++; sum++; break; }
                sum -= d[c];
                d[c] = 0;
            }
        }
        if (sum == 0) { ways[st] = 1.0; continue; }
        const uint8_t neq = a->tokens[unfixed[m - sum]].neq;
        double w = 0;
        for (int c = 0; c < 5; c++) {
            if (d[c] == 0 || (neq & (1 << c))) continue;
            w += d[c] * ways[st - stride[c]];
        }
        ways[st] = w;
    }
    int st = S - 1;
    for (int c = 0; c < 5; c++) d[c] = remaining[c];
    if (ways[st] <= 0) return 0;
    for (int i = 0; i < m; i++) {
        const uint8_t neq = a->tokens[unfixed[i]].neq;
        double u = a->uniform(a->ctx) * ways[st];
        int pick = -1;
        for (int c = 0; c < 5; c++) {
            if (d[c] == 0 || (neq & (1 << c))) continue;
            pick = c;
            u -= d[c] * ways[st - stride[c]];
            if (u < 0) break;
        }
        if (pick < 0) return 0;
        roles_out[unfixed[i]] = pick;
        st -= stride[pick];
        d[pick]--;
    }
    return 1;
}

double coup_xoshiro_uniform(void *ctx) {
    return (double)(xoshiro256_next((Xoshiro256 *)ctx) >> 11) * 0x1.0p-53;
}

int coup_resample(const CoupLogEvent *events, int len, int num_players,
                  int player, CoupUniformFn uniform, void *ctx,
                  uint8_t *actions_out, int max_attempts) {
    if (len < 0 || len > COUP_LOG_CAPACITY || player < 0 ||
        player >= num_players || num_players > MAX_PLAYERS)
        return -2;
    if (max_attempts <= 0) max_attempts = 100000;
    Attempt *a = (Attempt *)malloc(sizeof(Attempt));
    if (!a) return -2;
    a->ev = events;
    a->len = len;
    a->player = player;
    a->uniform = uniform;
    a->ctx = ctx;
    int result = -1;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        memset(a->picks, -1, (size_t)len);
        int rc = plan_lives(a);
        if (rc < 0) { result = -2; break; }
        if (rc == 0) continue;
        rc = flow_tokens(a);
        if (rc < 0) { result = -2; break; }
        if (rc == 0) continue;
        int roles[NTOK];
        if (!assign_roles(a, roles)) continue;
        for (int i = 0; i < len; i++) {
            if (coup_event_is_chance(&events[i]))
                actions_out[i] = (uint8_t)roles[a->token_at[i]];
            else if (a->picks[i] >= 0)
                actions_out[i] = (uint8_t)(ACT_DISCARD_SLOT0 + a->picks[i]);
            else
                actions_out[i] = events[i].action;
        }
        result = attempt;
        break;
    }
    free(a);
    return result;
}

int coup_resample_game(const CoupLogEvent *events, int len, int num_players,
                       int refund, int player, CoupUniformFn uniform,
                       void *ctx, Game *out, CoupLog *log_out) {
    uint8_t actions[COUP_LOG_CAPACITY];
    int rc = coup_resample(events, len, num_players, player, uniform, ctx,
                           actions, 0);
    if (rc < 0) return rc;
    game_init(out, num_players, 0, 0);
    game_set_refund_on_challenge(out, refund);
    if (log_out) coup_log_reset(log_out);
    for (int i = 0; i < len; i++)
        if (coup_step_logged(out, log_out, actions[i]) != 0) return -3;
    return rc;
}
