/*
 * test_search.c — tests for the opt-in search API (coup_search.h).
 *
 * Build/run: make test-c   (also under make test-san)
 */
#include "coup_search.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    abort(); } } while (0)

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int rnd(Xoshiro256 *r, int n) { return (int)xoshiro256_uniform(r, (uint32_t)n); }

static int random_legal(const Game *g, Xoshiro256 *r) {
    uint32_t m = get_valid_actions(g);
    CHECK(m != 0);
    int k = rnd(r, __builtin_popcount(m));
    while (k--) m &= m - 1;
    return __builtin_ctz(m);
}

static int random_chance(const Game *g, Xoshiro256 *r) {
    int k = rnd(r, deck_total(g));
    for (int c = 0; c < 5; c++) {
        k -= deck_count(g, c);
        if (k < 0) return c;
    }
    CHECK(0);
    return -1;
}

/* One random event (decision or chance outcome). */
static int random_event(const Game *g, Xoshiro256 *r) {
    return is_chance_node(g) ? random_chance(g, r) : random_legal(g, r);
}

static int terminal(const Game *g) {
    return get_phase(g) != PHASE_DEAL && is_done(g);
}

/* Random play from a new undealt game with a random refund flag, stopped
 * at a uniformly random point of the game (terminal included). */
static void random_state(Game *g, CoupLog *log, int n, Xoshiro256 *r) {
    int refund = rnd(r, 2);
    game_init(g, n, 0, 0);
    game_set_refund_on_challenge(g, refund);
    coup_log_reset(log);
    while (!terminal(g))
        CHECK(coup_step_logged(g, log, random_event(g, r)) == 0);
    int stop = rnd(r, log->len + 1);
    game_init(g, n, 0, 0);
    game_set_refund_on_challenge(g, refund);
    for (int i = 0; i < stop; i++)
        CHECK(coup_step_logged(g, NULL, log->ev[i].action) == 0);
    log->len = stop;
}

/* Deck-independent comparison of everything but the RNG. */
static int same_state(const Game *a, const Game *b) {
    return memcmp(a, b, offsetof(Game, rng)) == 0;
}

static void check_same_behavior(const Game *a, const Game *b) {
    CHECK(get_valid_actions(a) == get_valid_actions(b));
    CHECK(is_chance_node(a) == is_chance_node(b));
    CHECK(terminal(a) == terminal(b));
    CHECK(get_active_player_ext(a) == get_active_player_ext(b));
    if (is_chance_node(a)) {
        ChanceOutcome oa[MAX_CHANCE_OUTCOMES], ob[MAX_CHANCE_OUTCOMES];
        int na = chance_outcomes(a, oa), nb = chance_outcomes(b, ob);
        CHECK(na == nb);
        for (int i = 0; i < na; i++) {
            CHECK(oa[i].outcome == ob[i].outcome);
            CHECK(oa[i].prob == ob[i].prob);
        }
    }
}

static void check_public_equal(const Game *a, const Game *b) {
    CoupPublicState pa, pb;
    coup_public_from_game(a, &pa);
    coup_public_from_game(b, &pb);
    CHECK(coup_public_equal(&pa, &pb));
    CHECK(memcmp(&pa, &pb, sizeof(pa)) == 0);
    CHECK(coup_public_key(&pa) == coup_public_key(&pb));
}

static void check_hidden_equal(const Game *a, const Game *b) {
    uint8_t ha[MAX_PLAYERS][2], hb[MAX_PLAYERS][2];
    CoupExchangeHidden xa, xb;
    coup_hidden_from_game(a, ha, &xa);
    coup_hidden_from_game(b, hb, &xb);
    CHECK(memcmp(ha, hb, sizeof(ha)) == 0);
    CHECK(memcmp(&xa, &xb, sizeof(xa)) == 0);
    for (int c = 0; c < 5; c++) CHECK(deck_count(a, c) == deck_count(b, c));
}

/* Reassign every non-public card (hidden hand slots, exchange draws, deck)
 * by a random permutation, keeping the public state. Pokes Game bits
 * directly so it does not depend on coup_game_from_public. */
static void shuffle_hidden(Game *g, Xoshiro256 *r) {
    uint8_t hands[MAX_PLAYERS][2];
    CoupExchangeHidden xh;
    coup_hidden_from_game(g, hands, &xh);
    int pool[15], n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        for (int k = 0; k < 2; k++)
            if (hands[p][k] != COUP_CARD_NONE) pool[n++] = hands[p][k];
    for (int d = 0; d < 2; d++)
        if (xh.draw[d] != COUP_CARD_NONE) pool[n++] = xh.draw[d];
    for (int c = 0; c < 5; c++)
        for (int i = 0; i < deck_count(g, c); i++) pool[n++] = c;
    for (int i = n - 1; i > 0; i--) {
        int j = rnd(r, i + 1), t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
    }
    int i = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        for (int k = 0; k < 2; k++)
            if (hands[p][k] != COUP_CARD_NONE) {
                if (k) set_player_card1_type(g, p, pool[i++]);
                else   set_player_card0_type(g, p, pool[i++]);
            }
    if (xh.draw[0] != COUP_CARD_NONE) set_exchange_card0(g, pool[i++]);
    if (xh.draw[1] != COUP_CARD_NONE) set_exchange_card1(g, pool[i++]);
    for (int c = 0; c < 5; c++) deck_set_count(g, c, 0);
    for (; i < n; i++) deck_add(g, pool[i]);
}

/* ------------------------------------------------------------------ */
/* 1. Log                                                              */
/* ------------------------------------------------------------------ */

static CoupLog g_log, g_log2;

static void test_log_matches_engine(void) {
    Xoshiro256 r;
    xoshiro256_seed(&r, 1);
    int max_len = 0;
    for (int n = 2; n <= 6; n++) {
        for (int game = 0; game < 300; game++) {
            /* Undealt game whose chance nodes all come from g->rng: */
            Game a, b;
            game_init(&a, n, 0, 0);
            xoshiro256_seed(&a.rng, 1000u * n + game);
            game_set_refund_on_challenge(&a, game & 1);
            b = a;
            coup_log_reset(&g_log);
            step_with_rng(&a, 0);              /* resolves the deal */
            CHECK(coup_step_logged_rng(&b, &g_log, 0) == -1);
            CHECK(memcmp(&a, &b, sizeof(Game)) == 0);
            CHECK(g_log.len == 2 * n);
            while (!terminal(&a)) {
                int act = random_legal(&a, &r);
                CHECK(step_with_rng(&a, act) == 0);
                CHECK(coup_step_logged_rng(&b, &g_log, act) == 0);
                CHECK(memcmp(&a, &b, sizeof(Game)) == 0);
            }
            if (g_log.len > max_len) max_len = g_log.len;
            CHECK(g_log.len <= COUP_MAX_GAME_EVENTS(n));
            /* The log replays the game exactly. */
            Game c;
            game_init(&c, n, 0, 0);
            game_set_refund_on_challenge(&c, game & 1);
            for (int i = 0; i < g_log.len; i++)
                CHECK(coup_step_logged(&c, NULL, g_log.ev[i].action) == 0);
            CHECK(same_state(&a, &c));
            CHECK(coup_log_same_view(g_log.ev, g_log.ev, g_log.len, 0));
        }
    }
    printf("  log: longest random game %d events (capacity %d)\n", max_len,
           COUP_LOG_CAPACITY);
}

/* A long game: everyone exchanges and passes until MAX_TURNS. */
static void play_long_game(Game *g, CoupLog *log, int n, uint64_t seed) {
    game_init(g, n, 0, 0);
    xoshiro256_seed(&g->rng, seed);
    coup_log_reset(log);
    coup_step_logged_rng(g, log, 0); /* deal */
    while (!terminal(g)) {
        uint32_t m = get_valid_actions(g);
        int act = (m >> ACT_EXCHANGE & 1) ? ACT_EXCHANGE
                : (m >> ACT_PASS & 1) ? ACT_PASS : __builtin_ctz(m);
        CHECK(coup_step_logged_rng(g, log, act) == 0);
    }
}

static void test_long_game(void) {
    Game g;
    play_long_game(&g, &g_log, 6, 9);
    CHECK(g.turn_count == MAX_TURNS);
    CHECK(g_log.len == 12 + MAX_TURNS * (1 + 5 + 2 + 2));
    CHECK(g_log.len <= COUP_MAX_GAME_EVENTS(6));
    Xoshiro256 ur;
    xoshiro256_seed(&ur, 10);
    for (int p = 0; p < 6; p++) {
        Game s;
        CHECK(coup_resample_game(g_log.ev, g_log.len, 6, 1, p, coup_xoshiro_uniform,
                                 &ur, &s, &g_log2) >= 1);
        CHECK(coup_log_same_view(g_log.ev, g_log2.ev, g_log.len, p));
        check_public_equal(&g, &s);
    }
    printf("  long game: %d events (6p, MAX_TURNS)\n", g_log.len);
}

static void test_event_facts(void) {
    Game g;
    game_init(&g, 2, 0, 0);
    coup_log_reset(&g_log);
    /* p0: Duke Captain, p1: Assassin Contessa */
    int deal[4] = {DUKE, CAPTAIN, ASSASSIN, CONTESSA};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, deal[i]) == 0);
    CHECK(g_log.ev[2].phase == PHASE_DEAL && g_log.ev[2].actor == 1);
    CHECK(coup_step_logged(&g, &g_log, ACT_TAX) == 0);
    CHECK(coup_step_logged(&g, &g_log, ACT_CHALLENGE) == 0);
    const CoupLogEvent *e = &g_log.ev[5];
    CHECK(e->phase == PHASE_CHALLENGE_ACTION && e->actor == 1);
    CHECK(e->info == 1 && e->claimant == 0 && e->role == DUKE);
    /* p1 loses slot 1 (Contessa), then p0 redraws slot 0. */
    CHECK(coup_step_logged(&g, &g_log, ACT_DISCARD_SLOT1) == 0);
    e = &g_log.ev[6];
    CHECK(e->phase == PHASE_LOSE_CARD && e->info == CONTESSA);
    CHECK(is_chance_node(&g));
    CHECK(coup_step_logged(&g, &g_log, AMBASSADOR) == 0);
    e = &g_log.ev[7];
    CHECK(e->phase == PHASE_CHANCE_REDRAW && e->actor == 0 && e->info == 0);
    /* Illegal actions are rejected and not logged. */
    int len = g_log.len;
    CHECK(coup_step_logged(&g, &g_log, ACT_CHALLENGE) == -1);
    CHECK(coup_step_logged(&g, &g_log, 99) == -1);
    CHECK(g_log.len == len);
    CoupLogEvent tmp;
    CHECK(coup_event_make(&g, ACT_PASS, &tmp) == -1);
}

/* ------------------------------------------------------------------ */
/* 2-4. Public state, hands, construction                              */
/* ------------------------------------------------------------------ */

static void test_hand_space(void) {
    CHECK(coup_hand_count(0) == 1 && coup_hand_count(1) == 5 && coup_hand_count(2) == 15);
    int seen[15] = {0};
    for (int a = 0; a < 5; a++) {
        for (int b = 0; b < 5; b++) {
            int cards[2] = {a, b}, out[2];
            int idx = coup_hand_index(2, cards);
            CHECK(idx >= 0 && idx < 15);
            int swapped[2] = {b, a};
            CHECK(coup_hand_index(2, swapped) == idx);
            CHECK(coup_hand_cards(2, idx, out) == 0);
            CHECK(out[0] == (a < b ? a : b) && out[1] == (a < b ? b : a));
            seen[idx] = 1;
            CHECK(coup_hand_multiplicity(2, idx, a) == (a == b ? 2 : 1));
            CHECK(coup_hand_remove(2, idx, a) == b);
            CHECK(coup_hand_remove(2, idx, b) == a);
            CHECK(coup_hand_add(1, a, b) == idx);
            for (int r = 0; r < 5; r++) {
                int has = r == a || r == b;
                if (!has) CHECK(coup_hand_remove(2, idx, r) == -1);
                CHECK(coup_hand_challenge_consistent(2, idx, r, 1) == has);
                CHECK(coup_hand_challenge_consistent(2, idx, r, 0) == !has);
            }
        }
    }
    for (int i = 0; i < 15; i++) CHECK(seen[i]);
    for (int i = 0; i < 15; i++) {
        int c[2];
        CHECK(coup_hand_cards(2, i, c) == 0 && coup_hand_index(2, c) == i);
    }
    for (int a = 0; a < 5; a++) {
        int c[2] = {a, 0};
        CHECK(coup_hand_index(1, c) == a);
        CHECK(coup_hand_remove(1, a, a) == 0);
        CHECK(coup_hand_add(0, 0, a) == a);
    }
    CHECK(coup_hand_add(2, 0, 1) == -1);
    CHECK(coup_hand_cards(2, 15, (int[2]){0, 0}) == -1);
    int bad[2] = {5, 0};
    CHECK(coup_hand_index(2, bad) == -1);
}

/* Lockstep: the two games stay identical (except RNG) to the end. */
static void play_lockstep(Game *a, Game *b, Xoshiro256 *r) {
    while (!terminal(a)) {
        check_same_behavior(a, b);
        check_public_equal(a, b);
        check_hidden_equal(a, b);
        int act = random_event(a, r);
        CoupLogEvent ea, eb;
        CHECK(coup_event_make(a, act, &ea) == 0);
        CHECK(coup_event_make(b, act, &eb) == 0);
        CHECK(memcmp(&ea, &eb, sizeof(ea)) == 0);
        CHECK(coup_step_logged(a, NULL, act) == 0);
        CHECK(coup_step_logged(b, NULL, act) == 0);
    }
    CHECK(terminal(b));
    CHECK(get_winner(a) == get_winner(b));
    check_public_equal(a, b);
}

static void test_public_and_round_trip(void) {
    Xoshiro256 r;
    xoshiro256_seed(&r, 2);
    int states = 0, phases[10] = {0};
    CoupPublicState prev;
    memset(&prev, 0, sizeof(prev));
    for (int n = 2; n <= 6; n++) {
        for (int game = 0; game < 400; game++) {
            Game g;
            random_state(&g, &g_log, n, &r);
            states++;
            phases[get_phase(&g)]++;
            CoupPublicState pub;
            coup_public_from_game(&g, &pub);
            /* The pack is exact: different public states, different packs. */
            uint64_t k1[2], k2[2];
            coup_public_pack(&pub, k1);
            coup_public_pack(&prev, k2);
            CHECK((memcmp(&pub, &prev, sizeof(pub)) == 0) ==
                  (k1[0] == k2[0] && k1[1] == k2[1]));
            prev = pub;
            uint8_t hands[MAX_PLAYERS][2];
            CoupExchangeHidden xh;
            coup_hidden_from_game(&g, hands, &xh);

            /* Public-state queries agree with the engine. */
            CHECK(coup_public_is_chance(&pub) == is_chance_node(&g));
            CHECK(coup_public_is_terminal(&pub) == terminal(&g));
            CHECK(pub.deck_size == deck_total(&g));
            if (!(pub.phase == PHASE_EXCHANGE_DISCARD && pub.exchange_picks))
                CHECK(coup_public_valid_actions(&pub) == get_valid_actions(&g));
            int deck[5];
            CHECK(coup_deck_from_hidden(&pub, hands, &xh, deck) == 0);
            for (int c = 0; c < 5; c++) CHECK(deck[c] == deck_count(&g, c));
            for (int p = 0; p < n; p++) {
                int cards[2], idx;
                int size = coup_game_hand(&g, p, cards, &idx);
                CHECK(size == coup_public_hand_size(&pub, p));
                CHECK(idx >= 0 && idx < coup_hand_count(size));
            }

            /* Hidden-info invariance: shuffle everything hidden. */
            for (int t = 0; t < 4; t++) {
                Game alt = g;
                shuffle_hidden(&alt, &r);
                check_public_equal(&g, &alt);
                /* Coup's legal actions never depend on hidden cards. */
                CHECK(get_valid_actions(&alt) == get_valid_actions(&g));
                CHECK(get_active_player_ext(&alt) == get_active_player_ext(&g));
                /* ... and construction from (pub, alt hidden) gives alt. */
                uint8_t ah[MAX_PLAYERS][2];
                CoupExchangeHidden ax;
                coup_hidden_from_game(&alt, ah, &ax);
                Game built;
                CHECK(coup_game_from_public(&pub, ah, &ax, &built) == 0);
                check_hidden_equal(&alt, &built);
                CHECK(get_valid_actions(&built) == get_valid_actions(&g));
            }

            /* Round trip with the true assignment: identical game. */
            Game built;
            CHECK(coup_game_from_public(&pub, hands, &xh, &built) == 0);
            check_public_equal(&g, &built);
            check_hidden_equal(&g, &built);
            check_same_behavior(&g, &built);
            if (game % 4 == 0) {
                Game a = g;
                play_lockstep(&a, &built, &r);
            }
        }
    }
    printf("  public/round-trip: %d states; phases:", states);
    for (int i = 0; i < 9; i++) printf(" %d", phases[i]);
    printf("\n");
    for (int i = 0; i <= PHASE_EXCHANGE_DISCARD; i++) CHECK(phases[i] > 0);
}

static void test_from_public_rejects(void) {
    Game g;
    game_init(&g, 3, 7, 8);
    CoupPublicState pub;
    coup_public_from_game(&g, &pub);
    uint8_t hands[MAX_PLAYERS][2];
    CoupExchangeHidden xh;
    coup_hidden_from_game(&g, hands, &xh);
    Game out;
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == 0);
    /* Four Dukes. */
    hands[0][0] = hands[0][1] = hands[1][0] = hands[1][1] = DUKE;
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == -1);
    hands[0][0] = 5;
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == -1);
    int idx[MAX_PLAYERS] = {14, 14, 14, 0, 0, 0}; /* 6 Contessas */
    CHECK(coup_game_from_public_idx(&pub, idx, NULL, &out) == -1);
    int ok[MAX_PLAYERS] = {0, 14, 5, 0, 0, 0};    /* DD, CC, AA */
    CHECK(coup_game_from_public_idx(&pub, ok, NULL, &out) == 0);
    CHECK(get_valid_actions(&out) == get_valid_actions(&g));
    CHECK(player_card0_type(&out, 2) == ASSASSIN && player_card1_type(&out, 2) == ASSASSIN);

    /* Exchange in progress: xh required, first discard must be legal. */
    game_init(&g, 2, 0, 0);
    int deal[4] = {DUKE, CAPTAIN, ASSASSIN, CONTESSA};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, NULL, deal[i]) == 0);
    CHECK(coup_step_logged(&g, NULL, ACT_EXCHANGE) == 0);
    CHECK(coup_step_logged(&g, NULL, ACT_PASS) == 0);
    CHECK(coup_step_logged(&g, NULL, AMBASSADOR) == 0);
    CHECK(coup_step_logged(&g, NULL, DUKE) == 0);
    CHECK(coup_step_logged(&g, NULL, ACT_DISCARD_SLOT1) == 0);
    coup_public_from_game(&g, &pub);
    CHECK(pub.exchange_draws == 2 && pub.exchange_picks == 1);
    CHECK(coup_public_valid_actions(&pub) == 0);
    coup_hidden_from_game(&g, hands, &xh);
    CHECK(xh.first_discard == 1 && xh.draw[0] == AMBASSADOR && xh.draw[1] == DUKE);
    CHECK(coup_game_from_public(&pub, hands, NULL, &out) == -1);
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == 0);
    CHECK(get_valid_actions(&out) == get_valid_actions(&g));
    xh.first_discard = 3;
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == -1);
    xh.draw[0] = xh.draw[1] = DUKE; /* p0 holds a Duke too: 3 Dukes is fine */
    xh.first_discard = 1;
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == 0);
    hands[1][0] = DUKE; /* now 4 */
    CHECK(coup_game_from_public(&pub, hands, &xh, &out) == -1);
}

/* Label-free view: equal up to slot order within each hand. */
static void check_label_free_equal(const Game *a, const Game *b) {
    CHECK(get_phase(a) == get_phase(b));
    if (get_phase(a) == PHASE_LOSE_CARD || get_phase(a) == PHASE_EXCHANGE_DISCARD)
        /* these masks name slots */
        CHECK(__builtin_popcount(get_valid_actions(a)) ==
              __builtin_popcount(get_valid_actions(b)));
    else
        CHECK(get_valid_actions(a) == get_valid_actions(b));
    CHECK(get_turn_player(a) == get_turn_player(b));
    CHECK(get_active_player(a) == get_active_player(b));
    CHECK(get_pending_action(a) == get_pending_action(b));
    CHECK(a->turn_count == b->turn_count);
    CHECK(terminal(a) == terminal(b));
    for (int c = 0; c < 5; c++) CHECK(deck_count(a, c) == deck_count(b, c));
    for (int p = 0; p < get_num_players(a); p++) {
        CHECK(player_coins(a, p) == player_coins(b, p));
        int ca[2], cb[2], ia, ib;
        int sa = coup_game_hand(a, p, ca, &ia), sb = coup_game_hand(b, p, cb, &ib);
        CHECK(sa == sb && ia == ib);
        CoupPublicState pa, pb;
        coup_public_from_game(a, &pa);
        coup_public_from_game(b, &pb);
        int ra = 0, rb = 0; /* revealed multisets as role counts */
        for (int k = 0; k < 2; k++) {
            if (pa.revealed[p][k] < 5) ra += 1 << (3 * pa.revealed[p][k]);
            if (pb.revealed[p][k] < 5) rb += 1 << (3 * pb.revealed[p][k]);
        }
        CHECK(ra == rb);
    }
}

/* Map a hand-slot action of p in `a` to the slot holding the same card in
 * `b` (avoiding `taken`). */
static int map_slot(const Game *a, const Game *b, int p, int slot, int taken) {
    if (slot >= 2) return slot;
    int t = slot ? player_card1_type(a, p) : player_card0_type(a, p);
    for (int k = 0; k < 2; k++) {
        if (k == taken) continue;
        int alive = k ? player_card1_alive(b, p) : player_card0_alive(b, p);
        int tk = k ? player_card1_type(b, p) : player_card0_type(b, p);
        if (alive && tk == t) return k;
    }
    CHECK(0);
    return -1;
}

static void test_canonicalize(void) {
    Xoshiro256 r;
    xoshiro256_seed(&r, 3);
    int swaps = 0;
    for (int n = 2; n <= 6; n++) {
        for (int game = 0; game < 300; game++) {
            Game a;
            random_state(&a, &g_log, n, &r);
            Game b = a;
            int sw = coup_canonicalize_hands(&b);
            swaps += __builtin_popcount(sw);
            check_public_equal(&a, &b);
            CHECK(coup_canonicalize_hands(&b) == 0); /* idempotent */
            for (int p = 0; p < n; p++) {
                int c[2];
                if (coup_game_hand(&b, p, c, NULL) == 2 && (sw >> p & 1))
                    CHECK(c[0] < c[1]);
            }
            /* Play on with type-mapped actions: same game up to labels. */
            while (!terminal(&a)) {
                check_label_free_equal(&a, &b);
                int ph = get_phase(&a), p = get_active_player(&a);
                if (is_chance_node(&a)) {
                    int c = random_chance(&a, &r);
                    CHECK(coup_step_logged(&a, NULL, c) == 0);
                    CHECK(coup_step_logged(&b, NULL, c) == 0);
                } else if (ph == PHASE_LOSE_CARD) {
                    int act = random_legal(&a, &r);
                    int k = map_slot(&a, &b, p, act - ACT_DISCARD_SLOT0, -1);
                    CHECK(coup_step_logged(&a, NULL, act) == 0);
                    CHECK(coup_step_logged(&b, NULL, ACT_DISCARD_SLOT0 + k) == 0);
                } else if (ph == PHASE_EXCHANGE_DISCARD &&
                           get_first_discard(&a) == FIRST_DISCARD_NONE) {
                    int a1 = random_legal(&a, &r);
                    CHECK(coup_step_logged(&a, NULL, a1) == 0);
                    int s1 = a1 - ACT_DISCARD_SLOT0;
                    Game before = a;
                    int a2 = random_legal(&a, &r);
                    int s2 = a2 - ACT_DISCARD_SLOT0;
                    int m1 = map_slot(&before, &b, p, s1, -1);
                    int m2 = map_slot(&before, &b, p, s2, m1);
                    if (m1 > m2) { int t = m1; m1 = m2; m2 = t; }
                    CHECK(coup_step_logged(&a, NULL, a2) == 0);
                    CHECK(coup_step_logged(&b, NULL, ACT_DISCARD_SLOT0 + m1) == 0);
                    CHECK(coup_step_logged(&b, NULL, ACT_DISCARD_SLOT0 + m2) == 0);
                } else if (ph == PHASE_EXCHANGE_DISCARD) {
                    /* Canonicalized mid-exchange: first pick was 2 or the
                     * player was left alone, so slots match directly. */
                    int act = random_legal(&a, &r);
                    int k = map_slot(&a, &b, p, act - ACT_DISCARD_SLOT0, -1);
                    CHECK(coup_step_logged(&a, NULL, act) == 0);
                    CHECK(coup_step_logged(&b, NULL, ACT_DISCARD_SLOT0 + k) == 0);
                } else {
                    int act = random_legal(&a, &r);
                    CHECK(coup_step_logged(&a, NULL, act) == 0);
                    CHECK(coup_step_logged(&b, NULL, act) == 0);
                }
            }
            check_label_free_equal(&a, &b);
            CHECK(get_winner(&a) == get_winner(&b));
        }
    }
    CHECK(swaps > 100);
    printf("  canonicalize: %d hand swaps checked\n", swaps);
}

/* ------------------------------------------------------------------ */
/* 6. Resampling                                                       */
/* ------------------------------------------------------------------ */

static void test_resample_consistency(void) {
    Xoshiro256 r, ur;
    xoshiro256_seed(&r, 4);
    xoshiro256_seed(&ur, 5);
    long calls = 0, attempts = 0;
    for (int n = 2; n <= 6; n++) {
        for (int game = 0; game < (n == 2 ? 300 : 120); game++) {
            Game g;
            game_init(&g, n, 0, 0);
            int refund = rnd(&r, 2);
            game_set_refund_on_challenge(&g, refund);
            coup_log_reset(&g_log);
            while (!terminal(&g)) {
                if (!is_chance_node(&g) && rnd(&r, 5) == 0) {
                    int p = rnd(&r, n);
                    Game s;
                    int rc = coup_resample_game(g_log.ev, g_log.len, n, refund, p,
                                                coup_xoshiro_uniform, &ur, &s, &g_log2);
                    CHECK(rc >= 1);
                    calls++;
                    attempts += rc;
                    CHECK(g_log2.len == g_log.len);
                    CHECK(coup_log_same_view(g_log.ev, g_log2.ev, g_log.len, p));
                    check_public_equal(&g, &s);
                    /* Masks match, except an opponent's second exchange
                     * pick (depends on their resampled first pick). */
                    if (!(get_phase(&g) == PHASE_EXCHANGE_DISCARD &&
                          get_first_discard(&g) != FIRST_DISCARD_NONE &&
                          get_turn_player(&g) != p))
                        CHECK(get_valid_actions(&s) == get_valid_actions(&g));
                    /* p's own hand (slot by slot) and exchange are kept. */
                    uint8_t h1[MAX_PLAYERS][2], h2[MAX_PLAYERS][2];
                    CoupExchangeHidden x1, x2;
                    coup_hidden_from_game(&g, h1, &x1);
                    coup_hidden_from_game(&s, h2, &x2);
                    CHECK(memcmp(h1[p], h2[p], 2) == 0);
                    if (get_turn_player(&g) == p)
                        CHECK(memcmp(&x1, &x2, sizeof(x1)) == 0);
                    /* The resampled world plays on to the end. */
                    while (!terminal(&s))
                        CHECK(coup_step_logged(&s, NULL, random_event(&s, &r)) == 0);
                }
                CHECK(coup_step_logged(&g, &g_log, random_event(&g, &r)) == 0);
            }
        }
    }
    printf("  resample: %ld calls, %.3f attempts/call\n", calls,
           (double)attempts / calls);
    CHECK(coup_resample(g_log.ev, g_log.len, 6, 6, coup_xoshiro_uniform, &ur,
                        (uint8_t[COUP_LOG_CAPACITY]){0}, 0) == -2);
}

static void test_resample_distribution(void) {
    Xoshiro256 ur;
    xoshiro256_seed(&ur, 6);
    /* 2 players right after the deal, p0 holds Duke+Duke: p1's first card
     * is uniform over the 13 unseen cards (Duke 1/13, others 3/13). */
    Game g;
    game_init(&g, 2, 0, 0);
    coup_log_reset(&g_log);
    int deal[4] = {DUKE, DUKE, CAPTAIN, CAPTAIN};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, deal[i]) == 0);
    int counts[5] = {0};
    const int kSamples = 20000;
    for (int i = 0; i < kSamples; i++) {
        Game s;
        CHECK(coup_resample_game(g_log.ev, g_log.len, 2, 1, 0, coup_xoshiro_uniform,
                                 &ur, &s, NULL) >= 1);
        CHECK(player_card0_type(&s, 0) == DUKE && player_card1_type(&s, 0) == DUKE);
        counts[player_card0_type(&s, 1)]++;
    }
    for (int c = 0; c < 5; c++) {
        double expect = (c == DUKE ? 1.0 : 3.0) / 13.0;
        CHECK(fabs(counts[c] / (double)kSamples - expect) < 0.015);
    }

    /* Constraints: p1 was caught bluffing Duke, so no resample may give p1
     * a Duke; p0 sees p1 reveal Assassin. */
    game_init(&g, 2, 0, 0);
    coup_log_reset(&g_log);
    int deal2[4] = {CAPTAIN, CONTESSA, ASSASSIN, AMBASSADOR};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, deal2[i]) == 0);
    int acts[4] = {ACT_INCOME, ACT_TAX, ACT_CHALLENGE, ACT_DISCARD_SLOT0};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, acts[i]) == 0);
    CHECK(get_active_player_ext(&g) == 0);
    for (int i = 0; i < 2000; i++) {
        Game s;
        CHECK(coup_resample_game(g_log.ev, g_log.len, 2, 1, 0, coup_xoshiro_uniform,
                                 &ur, &s, NULL) >= 1);
        CHECK(player_card0_type(&s, 1) == ASSASSIN && !player_card0_alive(&s, 1));
        CHECK(player_card1_type(&s, 1) != DUKE);
    }

    /* Proven claim from slot 1 with slot 0 alive: slot 0 cannot hold the
     * role (the engine would have picked slot 0). p1 = Captain, Duke. */
    game_init(&g, 2, 0, 0);
    coup_log_reset(&g_log);
    int deal3[4] = {CONTESSA, CONTESSA, CAPTAIN, DUKE};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, deal3[i]) == 0);
    int acts3[4] = {ACT_INCOME, ACT_TAX, ACT_CHALLENGE, ACT_DISCARD_SLOT0};
    for (int i = 0; i < 4; i++) CHECK(coup_step_logged(&g, &g_log, acts3[i]) == 0);
    CHECK(get_phase(&g) == PHASE_CHANCE_REDRAW && get_exchange_card0(&g) == 1);
    CHECK(coup_step_logged(&g, &g_log, AMBASSADOR) == 0);
    for (int i = 0; i < 2000; i++) {
        Game s;
        CHECK(coup_resample_game(g_log.ev, g_log.len, 2, 1, 0, coup_xoshiro_uniform,
                                 &ur, &s, &g_log2) >= 1);
        CHECK(player_card0_type(&s, 1) != DUKE);
        CHECK(g_log2.ev[8].phase == PHASE_CHANCE_REDRAW && g_log2.ev[8].info == 1);
    }
}

static void bench_resample(void) {
    Xoshiro256 r, ur;
    xoshiro256_seed(&r, 7);
    xoshiro256_seed(&ur, 8);
    static uint8_t actions[COUP_LOG_CAPACITY];
    long calls = 0;
    double t_core = 0, t_game = 0;
    for (int sim = 0; sim < 200; sim++) {
        Game g;
        game_init(&g, 6, 0, 0);
        coup_log_reset(&g_log);
        while (!terminal(&g)) {
            if (!is_chance_node(&g)) {
                int p = get_active_player(&g);
                double t0 = now_sec();
                CHECK(coup_resample(g_log.ev, g_log.len, 6, p, coup_xoshiro_uniform,
                                    &ur, actions, 0) >= 1);
                double t1 = now_sec();
                Game s;
                CHECK(coup_resample_game(g_log.ev, g_log.len, 6, 1, p,
                                         coup_xoshiro_uniform, &ur, &s, NULL) >= 1);
                double t2 = now_sec();
                t_core += t1 - t0;
                t_game += t2 - t1;
                calls++;
            }
            CHECK(coup_step_logged(&g, &g_log, random_event(&g, &r)) == 0);
        }
    }
    printf("  resample 6p: %ld calls, %.2f us/call (actions), %.2f us/call (+replay to Game)\n",
           calls, 1e6 * t_core / calls, 1e6 * t_game / calls);
}

int main(void) {
    printf("test_search\n");
    test_hand_space();
    printf("  hand space OK\n");
    test_event_facts();
    test_log_matches_engine();
    test_long_game();
    printf("  log OK\n");
    test_public_and_round_trip();
    test_from_public_rejects();
    printf("  public state / construction OK\n");
    test_canonicalize();
    printf("  canonicalize OK\n");
    test_resample_consistency();
    test_resample_distribution();
    printf("  resample OK\n");
    bench_resample();
    printf("All search tests passed.\n");
    return 0;
}
