/*
 * test_core.c — Comprehensive tests for the Coup C engine.
 *
 * Build:
 *   cc -O2 -std=c11 -o test_core c_engine/test_core.c c_engine/coup_core.c -I c_engine/ -lm
 * Run:
 *   ./test_core
 */

#include "coup_core.h"
#include "heuristic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STEPS 10000

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int first_legal_action(const Game *g) {
    uint32_t mask = get_valid_actions(g);
    assert(mask != 0 && "No legal actions available");
    return __builtin_ctz(mask);
}

/* Play a full game with "always pick first legal action" policy.
   Uses step_with_rng so chance nodes are resolved automatically.
   Returns the winner (player index), or -1 on timeout. */
static int play_full_game_rng(int num_players, uint64_t deal_seed,
                              uint64_t proc_seed, int *steps_out) {
    Game g;
    game_init(&g, num_players, deal_seed, proc_seed);
    int steps = 0;
    while (!is_done(&g) && steps < MAX_STEPS) {
        assert(!is_chance_node(&g));
        int a = first_legal_action(&g);
        step_with_rng(&g, a);
        steps++;
    }
    if (steps_out) *steps_out = steps;
    return get_winner(&g);
}

/* Play a full game in OpenSpiel mode (step_deterministic + manual
   apply_chance). Returns the winner.
   We use game_init with deal_seed=seed to auto-deal, then play
   deterministic actions + manual chance resolution. */
static int play_full_game_deterministic(int num_players, uint64_t seed,
                                        int *steps_out) {
    Game g;
    game_init(&g, num_players, seed, seed + 1);

    int steps = 0;
    while (!is_done(&g) && steps < MAX_STEPS) {
        if (is_chance_node(&g)) {
            ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
            int n = chance_outcomes(&g, outcomes);
            assert(n > 0);
            /* Pick first outcome */
            apply_chance(&g, outcomes[0].outcome);
        } else {
            int a = first_legal_action(&g);
            step_deterministic(&g, a);
        }
        steps++;
    }
    if (steps_out) *steps_out = steps;
    return get_winner(&g);
}

/* Count total cards in the whole game (deck + alive player hands) */
static int total_cards_in_game(const Game *g) {
    int total = deck_total(g);
    int np = get_num_players(g);
    for (int p = 0; p < np; p++) {
        if (player_card0_alive(g, p)) total++;
        if (player_card1_alive(g, p)) total++;
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Test 1: Deterministic deal                                          */
/* ------------------------------------------------------------------ */

static void test_deterministic_deal(void) {
    printf("test_deterministic_deal ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    /* After deal: phase should be MAIN_ACTION */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(get_num_players(&g) == 2);

    /* 2-player rule: starting player gets 1 coin, the other 2 */
    assert(player_coins(&g, 0) == 1);
    assert(player_coins(&g, 1) == 2);

    /* Each player has 2 alive cards */
    assert(player_card0_alive(&g, 0) == 1);
    assert(player_card1_alive(&g, 0) == 1);
    assert(player_card0_alive(&g, 1) == 1);
    assert(player_card1_alive(&g, 1) == 1);

    /* Card types must be valid (0-4) */
    assert(player_card0_type(&g, 0) >= 0 && player_card0_type(&g, 0) <= 4);
    assert(player_card1_type(&g, 0) >= 0 && player_card1_type(&g, 0) <= 4);
    assert(player_card0_type(&g, 1) >= 0 && player_card0_type(&g, 1) <= 4);
    assert(player_card1_type(&g, 1) >= 0 && player_card1_type(&g, 1) <= 4);

    /* Deck should have 15 - 4 = 11 cards for 2 players */
    assert(deck_total(&g) == 11);

    /* Total cards in game = 15 */
    assert(total_cards_in_game(&g) == 15);

    /* Deterministic: same seed gives same deal */
    Game g2;
    game_init(&g2, 2, 42, 99);
    assert(player_card0_type(&g, 0) == player_card0_type(&g2, 0));
    assert(player_card1_type(&g, 0) == player_card1_type(&g2, 0));
    assert(player_card0_type(&g, 1) == player_card0_type(&g2, 1));
    assert(player_card1_type(&g, 1) == player_card1_type(&g2, 1));

    /* Different seed gives (likely) different deal */
    Game g3;
    game_init(&g3, 2, 123, 99);
    /* Not a hard assert — could in theory collide, but extremely unlikely
       with 15-choose-4 possibilities */
    int same = (player_card0_type(&g, 0) == player_card0_type(&g3, 0)) &&
               (player_card1_type(&g, 0) == player_card1_type(&g3, 0)) &&
               (player_card0_type(&g, 1) == player_card0_type(&g3, 1)) &&
               (player_card1_type(&g, 1) == player_card1_type(&g3, 1));
    assert(!same && "Different seeds should give different deals");

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 2: Full game rollout (first-legal-action policy)               */
/* ------------------------------------------------------------------ */

static void test_full_game_rollout(void) {
    printf("test_full_game_rollout ... ");

    /* 2-player */
    for (uint64_t seed = 1; seed <= 5; seed++) {
        int steps;
        int winner = play_full_game_rng(2, seed, seed + 100, &steps);
        assert(winner >= 0 && winner < 2);
        assert(steps < MAX_STEPS);
    }

    /* 6-player */
    for (uint64_t seed = 1; seed <= 5; seed++) {
        int steps;
        int winner = play_full_game_rng(6, seed, seed + 100, &steps);
        assert(winner >= 0 && winner < 6);
        assert(steps < MAX_STEPS);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 3: Mask correctness                                            */
/* ------------------------------------------------------------------ */

static void test_mask_correctness(void) {
    printf("test_mask_correctness ... ");

    /* 3a: 10+ coins => only coup targets valid */
    {
        Game g;
        game_init(&g, 3, 10, 10);
        /* Give player 0 exactly 10 coins */
        set_player_coins(&g, 0, 10);
        /* Must be player 0's turn at MAIN_ACTION */
        assert(get_phase(&g) == PHASE_MAIN_ACTION);
        assert(get_turn_player(&g) == 0);

        uint32_t mask = get_valid_actions(&g);
        /* Should only have coup bits set (for alive opponents) */
        assert(!(mask & (1u << ACT_INCOME)));
        assert(!(mask & (1u << ACT_FOREIGN_AID)));
        assert(!(mask & (1u << ACT_TAX)));
        assert(!(mask & (1u << ACT_EXCHANGE)));
        /* At least one coup target must exist */
        uint32_t coup_mask = 0;
        for (int i = 0; i < 3; i++) {
            if (i != 0 && player_is_alive(&g, i))
                coup_mask |= (1u << (ACT_COUP_P0 + i));
        }
        assert(coup_mask != 0);
        assert(mask == coup_mask);
    }

    /* 3b: Dead players never targetable */
    {
        Game g;
        game_init(&g, 3, 20, 20);
        /* Kill player 2 */
        set_player_card0_alive(&g, 2, 0);
        set_player_card1_alive(&g, 2, 0);

        uint32_t mask = get_valid_actions(&g);
        /* No action targeting player 2 should be in the mask */
        assert(!(mask & (1u << (ACT_COUP_P0 + 2))));
        assert(!(mask & (1u << (ACT_STEAL_P0 + 2))));
        assert(!(mask & (1u << (ACT_ASSASSINATE_P0 + 2))));
    }

    /* 3c: Phase-appropriate actions (CHALLENGE_ACTION phase) */
    {
        Game g;
        game_init(&g, 2, 30, 30);
        /* Player 0 plays tax => goes to CHALLENGE_ACTION */
        step_with_rng(&g, ACT_TAX);
        assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);
        uint32_t mask = get_valid_actions(&g);
        /* Only CHALLENGE and PASS should be valid */
        assert(mask == ((1u << ACT_CHALLENGE) | (1u << ACT_PASS)));
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 4: Challenge resolution — success (claimant has the card)      */
/* ------------------------------------------------------------------ */

static void test_challenge_success(void) {
    printf("test_challenge_success ... ");

    Game g;
    game_init(&g, 2, 1, 1);

    /* Force player 0 to have a Duke, player 1 has anything */
    set_player_card0_type(&g, 0, DUKE);
    set_player_card1_type(&g, 0, CAPTAIN);
    set_player_card0_type(&g, 1, ASSASSIN);
    set_player_card1_type(&g, 1, CONTESSA);

    /* Fix deck to match: originally 3 of each = 15, minus 4 dealt.
       We set specific cards so we need to recount deck.
       Dealt: DUKE, CAPTAIN, ASSASSIN, CONTESSA
       Deck: DUKE*2, ASSASSIN*2, CAPTAIN*2, AMBASSADOR*3, CONTESSA*2 */
    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, ASSASSIN, 2);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, AMBASSADOR, 3);
    deck_set_count(&g, CONTESSA, 2);
    assert(deck_total(&g) == 11);

    /* Player 0 claims Tax (Duke) — they do have Duke */
    step_deterministic(&g, ACT_TAX);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);
    assert(get_active_player(&g) == 1);

    /* Player 1 challenges */
    step_deterministic(&g, ACT_CHALLENGE);

    /* Claimant (player 0) has Duke => challenger (player 1) loses influence */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 1);

    /* Player 1 discards slot 0 */
    step_deterministic(&g, ACT_DISCARD_SLOT0);

    /* After lose card, claimant goes through CHANCE_REDRAW */
    assert(get_phase(&g) == PHASE_CHANCE_REDRAW);
    assert(get_active_player(&g) == 0);

    /* Player 0's Duke was put back in deck for reshuffling */
    /* Deck now has one more Duke (the claimed card returned) but we're
       about to draw a replacement. The total cards should stay consistent. */

    /* Apply a chance outcome — draw from deck */
    ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
    int n = chance_outcomes(&g, outcomes);
    assert(n > 0);
    apply_chance(&g, outcomes[0].outcome);

    /* Player 0 should still have 2 alive cards */
    assert(player_card0_alive(&g, 0) == 1);
    assert(player_card1_alive(&g, 0) == 1);

    /* Player 1 lost one card */
    assert(player_card0_alive(&g, 1) == 0);
    assert(player_card1_alive(&g, 1) == 1);

    /* Total cards in game should still be 15 */
    /* (11 deck - 1 dealt to player 0 replacement + 1 Duke returned = 11,
        but actually the redraw handles it) */
    /* After: 4 alive player cards (p0 has 2, p1 has 1 alive),
       plus some dead cards that don't count, plus deck. */
    /* Let's check: alive cards = 3, deck should be 15-4 = 11 (p1 lost a card
       but it's still in their hand, just dead). Actually dead cards aren't
       returned to deck. Total alive cards + deck = 15 - (dead cards). */
    /* Better check: alive in hands + deck */
    int alive_in_hands = 0;
    for (int p = 0; p < 2; p++) {
        if (player_card0_alive(&g, p)) alive_in_hands++;
        if (player_card1_alive(&g, p)) alive_in_hands++;
    }
    assert(alive_in_hands + deck_total(&g) == 14);
    /* 14 because 1 card is dead but still "in" the hand (15 - 1 dead) */

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 5: Challenge resolution — failure (claimant bluffing)          */
/* ------------------------------------------------------------------ */

static void test_challenge_failure(void) {
    printf("test_challenge_failure ... ");

    Game g;
    game_init(&g, 2, 2, 2);

    /* Force player 0 to NOT have a Duke */
    set_player_card0_type(&g, 0, CAPTAIN);
    set_player_card1_type(&g, 0, ASSASSIN);
    set_player_card0_type(&g, 1, CONTESSA);
    set_player_card1_type(&g, 1, AMBASSADOR);

    /* Fix deck: dealt CAPTAIN, ASSASSIN, CONTESSA, AMBASSADOR */
    deck_set_count(&g, DUKE, 3);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, ASSASSIN, 2);
    deck_set_count(&g, CONTESSA, 2);
    deck_set_count(&g, AMBASSADOR, 2);
    assert(deck_total(&g) == 11);

    /* Player 0 claims Tax (Duke) — BLUFF! They don't have Duke */
    step_deterministic(&g, ACT_TAX);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* Player 1 challenges */
    step_deterministic(&g, ACT_CHALLENGE);

    /* Claimant (player 0) does NOT have Duke => claimant loses influence */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 0);

    /* Player 0 discards slot 0 */
    step_deterministic(&g, ACT_DISCARD_SLOT0);
    assert(player_card0_alive(&g, 0) == 0);

    /* Action should be cancelled — turn advances */
    /* Player 1 should still have both cards */
    assert(player_card0_alive(&g, 1) == 1);
    assert(player_card1_alive(&g, 1) == 1);

    /* It should be player 1's turn now */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(get_turn_player(&g) == 1);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 6: Block resolution                                            */
/* ------------------------------------------------------------------ */

static void test_block_resolution(void) {
    printf("test_block_resolution ... ");

    /* 6a: Honest block unchallenged — action cancelled */
    {
        Game g;
        game_init(&g, 2, 50, 50);

        /* Player 0 attempts foreign aid */
        int coins_before = player_coins(&g, 0);
        step_deterministic(&g, ACT_FOREIGN_AID);
        assert(get_phase(&g) == PHASE_BLOCK);
        assert(get_active_player(&g) == 1);

        /* Player 1 blocks with Duke */
        step_deterministic(&g, ACT_BLOCK_DUKE);
        assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK);

        /* Player 0 passes (doesn't challenge the block) */
        step_deterministic(&g, ACT_PASS);

        /* Block stands — action cancelled, turn advances */
        assert(get_phase(&g) == PHASE_MAIN_ACTION);
        assert(get_turn_player(&g) == 1);
        /* Player 0 should NOT have gained 2 coins */
        assert(player_coins(&g, 0) == coins_before);
    }

    /* 6b: Bluff block challenged — blocker loses */
    {
        Game g;
        game_init(&g, 2, 60, 60);

        /* Force player 1 to NOT have Duke */
        set_player_card0_type(&g, 1, CAPTAIN);
        set_player_card1_type(&g, 1, ASSASSIN);
        /* Fix deck */
        deck_set_count(&g, DUKE, 3);
        deck_set_count(&g, CAPTAIN, 2);
        deck_set_count(&g, ASSASSIN, 2);
        /* Set p0 cards */
        set_player_card0_type(&g, 0, CONTESSA);
        set_player_card1_type(&g, 0, AMBASSADOR);
        deck_set_count(&g, CONTESSA, 2);
        deck_set_count(&g, AMBASSADOR, 2);

        int coins_before = player_coins(&g, 0);
        step_deterministic(&g, ACT_FOREIGN_AID);
        assert(get_phase(&g) == PHASE_BLOCK);

        /* Player 1 blocks with Duke (BLUFF) */
        step_deterministic(&g, ACT_BLOCK_DUKE);
        assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK);

        /* Player 0 challenges */
        step_deterministic(&g, ACT_CHALLENGE);

        /* Blocker (player 1) doesn't have Duke => blocker loses influence */
        assert(get_phase(&g) == PHASE_LOSE_CARD);
        assert(get_active_player(&g) == 1);

        step_deterministic(&g, ACT_DISCARD_SLOT0);

        /* Block failed => action proceeds (foreign aid resolves) */
        /* Resolve should give player 0 +2 coins */
        assert(player_coins(&g, 0) == coins_before + 2);
        /* Turn should advance */
        assert(get_phase(&g) == PHASE_MAIN_ACTION);
        assert(get_turn_player(&g) == 1);
    }

    /* 6c: Block challenged successfully — challenger loses, block stands */
    {
        Game g;
        game_init(&g, 2, 70, 70);

        /* Force player 1 to have Duke (honest block) */
        set_player_card0_type(&g, 1, DUKE);
        set_player_card1_type(&g, 1, CAPTAIN);
        set_player_card0_type(&g, 0, ASSASSIN);
        set_player_card1_type(&g, 0, CONTESSA);
        deck_set_count(&g, DUKE, 2);
        deck_set_count(&g, CAPTAIN, 2);
        deck_set_count(&g, ASSASSIN, 2);
        deck_set_count(&g, CONTESSA, 2);
        deck_set_count(&g, AMBASSADOR, 3);

        int coins_before = player_coins(&g, 0);
        step_deterministic(&g, ACT_FOREIGN_AID);
        assert(get_phase(&g) == PHASE_BLOCK);

        /* Player 1 blocks with Duke (honest) */
        step_deterministic(&g, ACT_BLOCK_DUKE);
        assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK);

        /* Player 0 challenges (bad idea!) */
        step_deterministic(&g, ACT_CHALLENGE);

        /* Blocker HAS Duke => challenger (player 0) loses influence */
        assert(get_phase(&g) == PHASE_LOSE_CARD);
        assert(get_active_player(&g) == 0);

        step_deterministic(&g, ACT_DISCARD_SLOT0);

        /* Now blocker gets a redraw (CHANCE_REDRAW) */
        assert(get_phase(&g) == PHASE_CHANCE_REDRAW);
        ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, outcomes);
        assert(n > 0);
        apply_chance(&g, outcomes[0].outcome);

        /* Block stands => action cancelled, player 0 doesn't get coins */
        assert(player_coins(&g, 0) == coins_before);
        /* Turn should advance */
        assert(get_phase(&g) == PHASE_MAIN_ACTION);
        assert(get_turn_player(&g) == 1);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 7: Ambassador exchange                                         */
/* ------------------------------------------------------------------ */

static void test_ambassador_exchange(void) {
    printf("test_ambassador_exchange ... ");

    Game g;
    game_init(&g, 2, 80, 80);

    /* Force player 0 to have Ambassador */
    set_player_card0_type(&g, 0, AMBASSADOR);
    set_player_card1_type(&g, 0, CAPTAIN);
    set_player_card0_type(&g, 1, DUKE);
    set_player_card1_type(&g, 1, CONTESSA);
    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, ASSASSIN, 3);
    deck_set_count(&g, AMBASSADOR, 2);
    deck_set_count(&g, CONTESSA, 2);
    assert(deck_total(&g) == 11);

    /* Player 0 plays Exchange */
    step_deterministic(&g, ACT_EXCHANGE);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* Player 1 passes (no challenge) */
    step_deterministic(&g, ACT_PASS);

    /* Should be in PHASE_CHANCE_EXCHANGE now — draw 2 cards */
    assert(get_phase(&g) == PHASE_CHANCE_EXCHANGE);

    /* Draw first card */
    ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
    int n = chance_outcomes(&g, outcomes);
    assert(n > 0);
    int drawn0 = outcomes[0].outcome;
    apply_chance(&g, drawn0);

    /* Still in CHANCE_EXCHANGE for second draw */
    assert(get_phase(&g) == PHASE_CHANCE_EXCHANGE);

    n = chance_outcomes(&g, outcomes);
    assert(n > 0);
    int drawn1 = outcomes[0].outcome;
    apply_chance(&g, drawn1);

    /* Now in EXCHANGE_DISCARD */
    assert(get_phase(&g) == PHASE_EXCHANGE_DISCARD);

    /* Verify drawn cards stored correctly */
    assert(get_exchange_card0(&g) == drawn0);
    assert(get_exchange_card1(&g) == drawn1);

    int deck_before_discard = deck_total(&g);

    /* Discard the two drawn cards (slots 2 and 3) — keep hand cards */
    /* First discard: slot 2 */
    uint32_t mask = get_valid_actions(&g);
    assert(mask & (1u << ACT_DISCARD_SLOT2));
    step_deterministic(&g, ACT_DISCARD_SLOT2);

    /* Second discard: must be slot > 2, so slot 3 */
    mask = get_valid_actions(&g);
    assert(mask & (1u << ACT_DISCARD_SLOT3));
    /* Verify canonical ordering: only slots > first discard are available */
    assert(!(mask & (1u << ACT_DISCARD_SLOT0)));
    assert(!(mask & (1u << ACT_DISCARD_SLOT1)));
    assert(!(mask & (1u << ACT_DISCARD_SLOT2)));
    step_deterministic(&g, ACT_DISCARD_SLOT3);

    /* Turn should advance */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(get_turn_player(&g) == 1);

    /* Deck should have gotten 2 cards back (the discards) minus 2 drawn = net 0,
       but we drew 2 then put 2 back */
    assert(deck_total(&g) == deck_before_discard + 2);

    /* Player 0 should still have 2 alive cards */
    assert(player_card0_alive(&g, 0) == 1);
    assert(player_card1_alive(&g, 0) == 1);

    /* Player keeps original hand (Ambassador, Captain) since we discarded
       drawn cards */
    assert(player_card0_type(&g, 0) == AMBASSADOR);
    assert(player_card1_type(&g, 0) == CAPTAIN);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 8: Player elimination — dead players skipped in cycling        */
/* ------------------------------------------------------------------ */

static void test_player_elimination_cycling(void) {
    printf("test_player_elimination_cycling ... ");

    Game g;
    game_init(&g, 4, 90, 90);

    /* Kill player 1 and player 3 */
    set_player_card0_alive(&g, 1, 0);
    set_player_card1_alive(&g, 1, 0);
    set_player_card0_alive(&g, 3, 0);
    set_player_card1_alive(&g, 3, 0);

    assert(!player_is_alive(&g, 1));
    assert(!player_is_alive(&g, 3));

    /* Player 0's turn — play Tax => CHALLENGE_ACTION cycling */
    assert(get_turn_player(&g) == 0);
    step_deterministic(&g, ACT_TAX);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* Active player should be player 2 (skipping dead player 1) */
    assert(get_active_player(&g) == 2);

    /* Player 2 passes */
    step_deterministic(&g, ACT_PASS);

    /* All alive non-turn players responded; should resolve */
    /* (players 1, 3 dead; player 2 passed; only turn player 0 excluded) */
    /* Tax resolves => coins increase => advance turn */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);

    /* Turn should skip dead players: next alive after 0 is 2 */
    assert(get_turn_player(&g) == 2);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 9: 2-player game completion                                    */
/* ------------------------------------------------------------------ */

static void test_2player_completion(void) {
    printf("test_2player_completion ... ");

    for (uint64_t seed = 1; seed <= 20; seed++) {
        int steps;
        int winner = play_full_game_rng(2, seed, seed * 7, &steps);
        assert(winner == 0 || winner == 1);
        assert(steps > 0 && steps < MAX_STEPS);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 10: 6-player game completion                                   */
/* ------------------------------------------------------------------ */

static void test_6player_completion(void) {
    printf("test_6player_completion ... ");

    for (uint64_t seed = 1; seed <= 20; seed++) {
        int steps;
        int winner = play_full_game_rng(6, seed, seed * 13, &steps);
        assert(winner >= 0 && winner < 6);
        assert(steps > 0 && steps < MAX_STEPS);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 11: Foreign aid block cycling                                   */
/* ------------------------------------------------------------------ */

static void test_foreign_aid_block_cycling(void) {
    printf("test_foreign_aid_block_cycling ... ");

    Game g;
    game_init(&g, 4, 100, 100);

    /* All 4 players alive. Player 0 plays foreign aid. */
    assert(get_turn_player(&g) == 0);
    step_deterministic(&g, ACT_FOREIGN_AID);

    /* Foreign aid goes directly to BLOCK (not CHALLENGE_ACTION) */
    assert(get_phase(&g) == PHASE_BLOCK);

    /* Each non-acting alive player should get a chance to block */
    int block_offered_to[4] = {0, 0, 0, 0};

    /* First blocker */
    int ap = get_active_player(&g);
    assert(ap != 0); /* not the turn player */
    block_offered_to[ap] = 1;
    step_deterministic(&g, ACT_PASS);

    /* Second blocker */
    if (get_phase(&g) == PHASE_BLOCK) {
        ap = get_active_player(&g);
        assert(ap != 0);
        block_offered_to[ap] = 1;
        step_deterministic(&g, ACT_PASS);
    }

    /* Third blocker */
    if (get_phase(&g) == PHASE_BLOCK) {
        ap = get_active_player(&g);
        assert(ap != 0);
        block_offered_to[ap] = 1;
        step_deterministic(&g, ACT_PASS);
    }

    /* All 3 opponents (1, 2, 3) should have been offered the block */
    assert(block_offered_to[1] == 1);
    assert(block_offered_to[2] == 1);
    assert(block_offered_to[3] == 1);

    /* After all pass, foreign aid resolves */
    assert(get_phase(&g) == PHASE_MAIN_ACTION ||
           get_phase(&g) == PHASE_RESOLVE);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test 12: Chance node enumeration                                    */
/* ------------------------------------------------------------------ */

static void test_chance_node_enumeration(void) {
    printf("test_chance_node_enumeration ... ");

    /* Test during DEAL phase */
    {
        Game g;
        game_init(&g, 2, 0, 0);  /* seeds=0 => no auto-deal */
        assert(get_phase(&g) == PHASE_DEAL);
        assert(is_chance_node(&g));

        ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, outcomes);
        assert(n > 0 && n <= 5);

        /* Probabilities should sum to 1.0 */
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            assert(outcomes[i].prob > 0.0);
            assert(outcomes[i].prob <= 1.0);
            sum += outcomes[i].prob;
        }
        assert(fabs(sum - 1.0) < 1e-9);

        /* Every outcome should correspond to a card type with non-zero deck count */
        for (int i = 0; i < n; i++) {
            int card = outcomes[i].outcome;
            assert(card >= 0 && card <= 4);
            assert(deck_count(&g, card) > 0);
        }

        /* Initially all 5 types have 3 copies, so all 5 should appear */
        assert(n == 5);
        /* Each probability = 3/15 = 0.2 */
        for (int i = 0; i < n; i++) {
            assert(fabs(outcomes[i].prob - 0.2) < 1e-9);
        }
    }

    /* Test after removing some cards */
    {
        Game g;
        game_init(&g, 2, 0, 0);
        /* Remove all Dukes from deck */
        deck_set_count(&g, DUKE, 0);
        /* Now deck has 12 cards, 4 types */

        ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, outcomes);
        assert(n == 4);

        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            assert(outcomes[i].outcome != DUKE);
            sum += outcomes[i].prob;
        }
        assert(fabs(sum - 1.0) < 1e-9);
    }

    /* Test during CHANCE_EXCHANGE (after exchange action resolves) */
    {
        Game g;
        game_init(&g, 2, 200, 200);

        /* Force player 0 to have Ambassador */
        set_player_card0_type(&g, 0, AMBASSADOR);
        set_player_card1_type(&g, 0, CAPTAIN);
        set_player_card0_type(&g, 1, DUKE);
        set_player_card1_type(&g, 1, CONTESSA);
        deck_set_count(&g, DUKE, 2);
        deck_set_count(&g, CAPTAIN, 2);
        deck_set_count(&g, ASSASSIN, 3);
        deck_set_count(&g, AMBASSADOR, 2);
        deck_set_count(&g, CONTESSA, 2);

        /* Exchange -> challenge phase -> pass -> CHANCE_EXCHANGE */
        step_deterministic(&g, ACT_EXCHANGE);
        step_deterministic(&g, ACT_PASS);
        assert(get_phase(&g) == PHASE_CHANCE_EXCHANGE);
        assert(is_chance_node(&g));

        ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, outcomes);
        assert(n > 0);

        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            sum += outcomes[i].prob;
        }
        assert(fabs(sum - 1.0) < 1e-9);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: OpenSpiel-style deterministic game                            */
/* ------------------------------------------------------------------ */

static void test_deterministic_game(void) {
    printf("test_deterministic_game ... ");

    /* 2-player */
    {
        int steps;
        int winner = play_full_game_deterministic(2, 42, &steps);
        assert(winner >= 0 && winner < 2);
        assert(steps > 0 && steps < MAX_STEPS);
    }

    /* 6-player */
    {
        int steps;
        int winner = play_full_game_deterministic(6, 42, &steps);
        assert(winner >= 0 && winner < 6);
        assert(steps > 0 && steps < MAX_STEPS);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: Card conservation invariant                                   */
/* ------------------------------------------------------------------ */

static void test_card_conservation(void) {
    printf("test_card_conservation ... ");

    /* Play several games and verify card counts stay consistent at each step */
    for (uint64_t seed = 1; seed <= 10; seed++) {
        Game g;
        game_init(&g, 4, seed, seed + 50);
        int initial_total = total_cards_in_game(&g);
        assert(initial_total == 15);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                /* Can't easily check during chance, just resolve */
                ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, outcomes);
                if (n > 0) apply_chance(&g, outcomes[0].outcome);
                else break;
            } else {
                int a = first_legal_action(&g);
                step_deterministic(&g, a);
            }
            /* Alive cards in hands + deck should never exceed 15 */
            int alive_total = total_cards_in_game(&g);
            assert(alive_total <= 15);
            steps++;
        }
        assert(steps < MAX_STEPS);
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: Exchange discard — picking slot 3 first must not deadlock     */
/* ------------------------------------------------------------------ */

static void test_exchange_discard_slot3_first(void) {
    printf("test_exchange_discard_slot3_first ... ");

    Game g;
    game_init(&g, 2, 80, 80);

    /* Skip to a deterministic state — advance past deal chance nodes */
    while (is_chance_node(&g)) {
        ChanceOutcome out[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, out);
        assert(n > 0);
        apply_chance(&g, out[0].outcome);
    }

    /* Force player 0 to have Ambassador (card 0) and some other card */
    set_player_card0_type(&g, 0, AMBASSADOR);
    set_player_card0_alive(&g, 0, 1);
    set_player_card1_type(&g, 0, CAPTAIN);
    set_player_card1_alive(&g, 0, 1);
    set_player_coins(&g, 0, 2);

    set_phase(&g, PHASE_MAIN_ACTION);
    set_turn_player(&g, 0);
    set_active_player(&g, 0);

    /* Player 0 takes Exchange */
    step_deterministic(&g, ACT_EXCHANGE);

    /* Advance through challenge phase — all pass */
    while (get_phase(&g) == PHASE_CHALLENGE_ACTION) {
        step_deterministic(&g, ACT_PASS);
    }

    /* Resolve chance nodes for exchange (draw 2 cards) */
    while (is_chance_node(&g)) {
        ChanceOutcome out[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(&g, out);
        assert(n > 0);
        apply_chance(&g, out[0].outcome);
    }

    /* Now in EXCHANGE_DISCARD */
    assert(get_phase(&g) == PHASE_EXCHANGE_DISCARD);

    /* First discard mask must NOT include slot 3 unless a higher slot exists.
     * Since slot 3 is the highest, it should only be offered if there's
     * a valid second pick — but with canonical ordering (strictly above),
     * there is none. So slot 3 must NOT appear in the first discard mask. */
    uint32_t mask1 = get_valid_actions(&g);
    assert(mask1 & (1u << ACT_DISCARD_SLOT0));  /* slot 0 OK (has higher) */
    assert(mask1 & (1u << ACT_DISCARD_SLOT1));  /* slot 1 OK (has higher) */
    assert(mask1 & (1u << ACT_DISCARD_SLOT2));  /* slot 2 OK (slot 3 is higher) */
    /* Slot 3 must NOT be in first discard mask: */
    assert(!(mask1 & (1u << ACT_DISCARD_SLOT3)));

    /* Pick slot 2 as first discard */
    step_deterministic(&g, ACT_DISCARD_SLOT2);

    /* Second discard: only slot 3 (strictly above 2) */
    uint32_t mask2 = get_valid_actions(&g);
    assert(mask2 != 0);  /* Must have at least one valid action */
    assert(mask2 & (1u << ACT_DISCARD_SLOT3));
    step_deterministic(&g, ACT_DISCARD_SLOT3);

    /* Should have advanced past EXCHANGE_DISCARD */
    assert(get_phase(&g) != PHASE_EXCHANGE_DISCARD);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: Exchange discard — second discard always has valid actions    */
/* ------------------------------------------------------------------ */

static void test_exchange_discard_no_deadlock(void) {
    printf("test_exchange_discard_no_deadlock ... ");

    Game g;
    game_init(&g, 2, 80, 80);

    /* Skip past deal */
    while (is_chance_node(&g)) {
        ChanceOutcome out[MAX_CHANCE_OUTCOMES];
        chance_outcomes(&g, out);
        apply_chance(&g, out[0].outcome);
    }

    /* Force state */
    set_player_card0_type(&g, 0, AMBASSADOR);
    set_player_card0_alive(&g, 0, 1);
    set_player_card1_type(&g, 0, DUKE);
    set_player_card1_alive(&g, 0, 1);
    set_player_coins(&g, 0, 2);

    set_phase(&g, PHASE_MAIN_ACTION);
    set_turn_player(&g, 0);
    set_active_player(&g, 0);

    step_deterministic(&g, ACT_EXCHANGE);

    while (get_phase(&g) == PHASE_CHALLENGE_ACTION)
        step_deterministic(&g, ACT_PASS);
    while (is_chance_node(&g)) {
        ChanceOutcome out[MAX_CHANCE_OUTCOMES];
        chance_outcomes(&g, out);
        apply_chance(&g, out[0].outcome);
    }

    assert(get_phase(&g) == PHASE_EXCHANGE_DISCARD);

    /* Try every valid first discard and confirm second discard is non-empty */
    uint32_t first_mask = get_valid_actions(&g);
    assert(first_mask != 0);

    for (int s = 0; s < 4; s++) {
        if (!(first_mask & (1u << (ACT_DISCARD_SLOT0 + s)))) continue;

        /* Clone game state to test this branch */
        Game g2 = g;
        step_deterministic(&g2, ACT_DISCARD_SLOT0 + s);

        uint32_t second_mask = get_valid_actions(&g2);
        if (second_mask == 0) {
            printf("FAIL: first discard slot %d leaves no second discard options\n", s);
            exit(1);
        }
    }

    printf("PASS\n");
}

/* ================================================================== */
/* Fuzz / stress tests                                                 */
/* ================================================================== */

/* Simple xorshift for test randomness (deterministic per seed) */
static uint32_t fuzz_rng_state;
static uint32_t fuzz_rand(void) {
    fuzz_rng_state ^= fuzz_rng_state << 13;
    fuzz_rng_state ^= fuzz_rng_state >> 17;
    fuzz_rng_state ^= fuzz_rng_state << 5;
    return fuzz_rng_state;
}

/* Pick a random legal action from the mask */
static int random_legal_action(const Game *g) {
    uint32_t mask = get_valid_actions(g);
    if (mask == 0) return -1;
    int count = __builtin_popcount(mask);
    int pick = fuzz_rand() % count;
    for (int i = 0; i < 32; i++) {
        if ((mask >> i) & 1) {
            if (pick == 0) return i;
            pick--;
        }
    }
    return -1; /* unreachable */
}

/* ------------------------------------------------------------------ */
/* Fuzz 1: valid actions are never empty for non-terminal states       */
/* ------------------------------------------------------------------ */

static void fuzz_valid_actions_nonempty(void) {
    printf("fuzz_valid_actions_nonempty ... ");
    int games_completed = 0;

    for (uint64_t seed = 1; seed <= 500; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 2654435761u);
        int np = 2 + (fuzz_rand() % 5);  /* 2-6 players */
        Game g;
        game_init(&g, np, seed, seed + 100);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) {
                    fprintf(stderr, "FAIL: chance_outcomes returned %d at seed=%llu step=%d phase=%d\n",
                            n, (unsigned long long)seed, steps, get_phase(&g));
                    exit(1);
                }
                /* Pick random outcome weighted by probability */
                int pick = fuzz_rand() % n;
                apply_chance(&g, out[pick].outcome);
            } else {
                uint32_t mask = get_valid_actions(&g);
                if (mask == 0) {
                    fprintf(stderr, "FAIL: no valid actions at seed=%llu step=%d phase=%d "
                            "active=%d turn=%d done=%d\n",
                            (unsigned long long)seed, steps, get_phase(&g),
                            get_active_player(&g), get_turn_player(&g), is_done(&g));
                    exit(1);
                }
                int a = random_legal_action(&g);
                step_deterministic(&g, a);
            }
            steps++;
        }
        if (is_done(&g)) games_completed++;
    }

    printf("PASS (%d/%d games completed)\n", games_completed, 500);
}

/* ------------------------------------------------------------------ */
/* Fuzz 2: card conservation across random games                       */
/* ------------------------------------------------------------------ */

static void fuzz_card_conservation(void) {
    printf("fuzz_card_conservation ... ");

    for (uint64_t seed = 1; seed <= 200; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 1664525u + 1013904223u);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 50);

        /* Total cards should be 15 always (3 of each of 5 types) */
        int initial = total_cards_in_game(&g);
        if (initial != 15) {
            fprintf(stderr, "FAIL: initial card count %d != 15 at seed=%llu np=%d\n",
                    initial, (unsigned long long)seed, np);
            exit(1);
        }

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }

            int total = total_cards_in_game(&g);
            if (total > 15) {
                fprintf(stderr, "FAIL: card count %d > 15 at seed=%llu step=%d phase=%d\n",
                        total, (unsigned long long)seed, steps, get_phase(&g));
                exit(1);
            }
            steps++;
        }
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Fuzz 3: dead players are never the active player                    */
/* ------------------------------------------------------------------ */

static void fuzz_dead_player_never_active(void) {
    printf("fuzz_dead_player_never_active ... ");

    for (uint64_t seed = 1; seed <= 300; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 6364136223846793005ull);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 77);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int ap = get_active_player(&g);
                if (!player_is_alive(&g, ap)) {
                    fprintf(stderr, "FAIL: dead player %d is active at seed=%llu step=%d phase=%d\n",
                            ap, (unsigned long long)seed, steps, get_phase(&g));
                    exit(1);
                }
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }
            steps++;
        }
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Fuzz 4: coin bounds — no player has negative or > 12 coins          */
/* ------------------------------------------------------------------ */

static void fuzz_coin_bounds(void) {
    printf("fuzz_coin_bounds ... ");

    for (uint64_t seed = 1; seed <= 300; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 2862933555777941757ull);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 33);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }

            for (int p = 0; p < np; p++) {
                int c = player_coins(&g, p);
                /* 12 = 9 + tax is the max reachable (must coup at 10+);
                 * > 12 would mean a wrap/overflow in the 4-bit field. */
                if (c < 0 || c > 12) {
                    fprintf(stderr, "FAIL: player %d has %d coins at seed=%llu step=%d\n",
                            p, c, (unsigned long long)seed, steps);
                    exit(1);
                }
            }
            steps++;
        }
    }

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Fuzz 5: chance node probabilities always sum to 1                   */
/* ------------------------------------------------------------------ */

static void fuzz_chance_probs_sum(void) {
    printf("fuzz_chance_probs_sum ... ");
    int chance_nodes_checked = 0;

    for (uint64_t seed = 1; seed <= 200; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 1103515245u + 12345u);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 22);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;

                double sum = 0.0;
                for (int i = 0; i < n; i++) {
                    if (out[i].prob < 0.0 || out[i].prob > 1.0) {
                        fprintf(stderr, "FAIL: invalid prob %f at seed=%llu\n",
                                out[i].prob, (unsigned long long)seed);
                        exit(1);
                    }
                    sum += out[i].prob;
                }
                if (fabs(sum - 1.0) > 1e-9) {
                    fprintf(stderr, "FAIL: chance probs sum to %f at seed=%llu step=%d\n",
                            sum, (unsigned long long)seed, steps);
                    exit(1);
                }
                chance_nodes_checked++;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }
            steps++;
        }
    }

    printf("PASS (%d chance nodes checked)\n", chance_nodes_checked);
}

/* ------------------------------------------------------------------ */
/* Fuzz 6: exactly one winner when game ends                           */
/* ------------------------------------------------------------------ */

static void fuzz_exactly_one_winner(void) {
    printf("fuzz_exactly_one_winner ... ");
    int games_completed = 0;

    for (uint64_t seed = 1; seed <= 500; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 3141592653u);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 11);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }
            steps++;
        }

        if (is_done(&g)) {
            games_completed++;
            int w = get_winner(&g);
            if (w < 0 || w >= np) {
                fprintf(stderr, "FAIL: invalid winner %d at seed=%llu np=%d\n",
                        w, (unsigned long long)seed, np);
                exit(1);
            }
            /* Winner must be alive */
            if (!player_is_alive(&g, w)) {
                fprintf(stderr, "FAIL: winner %d is dead at seed=%llu\n",
                        w, (unsigned long long)seed);
                exit(1);
            }
            /* All others must be dead */
            for (int p = 0; p < np; p++) {
                if (p == w) continue;
                if (player_is_alive(&g, p)) {
                    fprintf(stderr, "FAIL: non-winner %d is alive at seed=%llu\n",
                            p, (unsigned long long)seed);
                    exit(1);
                }
            }
        }
    }

    printf("PASS (%d/%d games completed)\n", games_completed, 500);
}

/* ------------------------------------------------------------------ */
/* Fuzz 7: forced coup at 10+ coins                                    */
/* ------------------------------------------------------------------ */

static void fuzz_forced_coup(void) {
    printf("fuzz_forced_coup ... ");
    int forced_coups_checked = 0;

    for (uint64_t seed = 1; seed <= 300; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 12345u);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 55);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int phase = get_phase(&g);
                int ap = get_active_player(&g);
                uint32_t mask = get_valid_actions(&g);

                if (phase == PHASE_MAIN_ACTION && player_coins(&g, ap) >= 10) {
                    forced_coups_checked++;
                    /* Only coup targets should be valid */
                    for (int a = 0; a < 32; a++) {
                        if ((mask >> a) & 1) {
                            if (a < ACT_COUP_P0 || a > ACT_COUP_P0 + 5) {
                                fprintf(stderr, "FAIL: player %d has 10+ coins but non-coup action %d valid "
                                        "at seed=%llu step=%d\n", ap, a, (unsigned long long)seed, steps);
                                exit(1);
                            }
                        }
                    }
                }

                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }
            steps++;
        }
    }

    printf("PASS (%d forced coup checks)\n", forced_coups_checked);
}

/* ------------------------------------------------------------------ */
/* Fuzz 8: step_with_rng random games — no crashes                     */
/* ------------------------------------------------------------------ */

static void fuzz_step_with_rng(void) {
    printf("fuzz_step_with_rng ... ");
    int games_completed = 0;

    for (uint64_t seed = 1; seed <= 1000; seed++) {
        fuzz_rng_state = (uint32_t)(seed);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 999);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            uint32_t mask = get_valid_actions(&g);
            if (mask == 0) {
                fprintf(stderr, "FAIL: step_with_rng no valid actions at seed=%llu step=%d phase=%d\n",
                        (unsigned long long)seed, steps, get_phase(&g));
                exit(1);
            }
            int a = random_legal_action(&g);
            step_with_rng(&g, a);
            steps++;
        }
        if (is_done(&g)) games_completed++;
    }

    printf("PASS (%d/%d games completed)\n", games_completed, 1000);
}

static void fuzz_no_info_leak(void) {
    printf("fuzz_no_info_leak ... ");
    int total_checks = 0;

    for (uint64_t seed = 1; seed <= 500; seed++) {
        int np = (seed % 3 == 0) ? 6 : 2;
        Game g;
        game_init(&g, np, seed, seed + 999);
        HistoryBuffer hist;
        history_init(&hist);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            int action = random_legal_action(&g);
            HistoryEntry entry;
            entry.acting_player = (uint8_t)get_active_player_ext(&g);
            entry.action = (uint8_t)action;
            entry.phase = (uint8_t)get_phase(&g);
            entry.result = 0;
            history_push(&hist, entry);
            step_with_rng(&g, action);
            steps++;

            if (is_done(&g)) break;

            /* For each observer, check that other players' alive cards
             * have zero type one-hots (no info leak). */
            for (int obs_player = 0; obs_player < np; obs_player++) {
                float obs[OBS_SIZE];
                observe(&g, obs_player, &hist, obs);

                for (int target = 0; target < np; target++) {
                    if (target == obs_player) continue;
                    int off = target * 12;

                    /* Card 0: if alive, type must be all zeros */
                    if (player_card0_alive(&g, target)) {
                        for (int t = 0; t < 5; t++) {
                            if (obs[off + t] != 0.0f) {
                                fprintf(stderr, "\nFAIL: info leak at seed=%llu step=%d "
                                        "observer=%d target=%d card0 type[%d]=%.1f (should be 0)\n",
                                        (unsigned long long)seed, steps, obs_player, target, t, obs[off + t]);
                                exit(1);
                            }
                        }
                        total_checks++;
                    }

                    /* Card 1: if alive, type must be all zeros */
                    if (player_card1_alive(&g, target)) {
                        for (int t = 0; t < 5; t++) {
                            if (obs[off + 6 + t] != 0.0f) {
                                fprintf(stderr, "\nFAIL: info leak at seed=%llu step=%d "
                                        "observer=%d target=%d card1 type[%d]=%.1f (should be 0)\n",
                                        (unsigned long long)seed, steps, obs_player, target, t, obs[off + 6 + t]);
                                exit(1);
                            }
                        }
                        total_checks++;
                    }
                }
            }
        }
    }

    printf("PASS (%d card visibility checks)\n", total_checks);
}

/* ------------------------------------------------------------------ */
/* Fuzz 11: game-end invariants — alive count and winner consistency   */
/* ------------------------------------------------------------------ */

/* Count alive players (alive_count is static in coup_core.c) */
static int count_alive(const Game *g) {
    int c = 0;
    int np = get_num_players(g);
    for (int i = 0; i < np; i++)
        if (player_is_alive(g, i)) c++;
    return c;
}

static void fuzz_game_end_invariants(void) {
    printf("fuzz_game_end_invariants ... ");
    int timeout_games = 0;
    int normal_games = 0;

    for (uint64_t seed = 1; seed <= 1000; seed++) {
        fuzz_rng_state = (uint32_t)(seed * 2718281828u);
        int np = 2 + (fuzz_rand() % 5);
        Game g;
        game_init(&g, np, seed, seed + 7);

        int steps = 0;
        int prev_alive = np;
        while (!is_done(&g) && steps < MAX_STEPS) {
            /* Alive count should never increase */
            int ac = count_alive(&g);
            if (ac > prev_alive) {
                fprintf(stderr, "FAIL: alive count increased from %d to %d at "
                        "seed=%llu step=%d\n", prev_alive, ac,
                        (unsigned long long)seed, steps);
                exit(1);
            }
            prev_alive = ac;

            /* Deck should never go negative */
            int dt = deck_total(&g);
            if (dt < 0) {
                fprintf(stderr, "FAIL: negative deck %d at seed=%llu step=%d\n",
                        dt, (unsigned long long)seed, steps);
                exit(1);
            }

            /* Total cards check — skip during chance/exchange phases where
               cards are temporarily in a buffer outside deck+hands */
            int phase = get_phase(&g);
            if (phase != PHASE_CHANCE_EXCHANGE && phase != PHASE_EXCHANGE_DISCARD &&
                phase != PHASE_CHANCE_REDRAW) {
                int alive_cards = 0;
                int dead_cards = 0;
                for (int p = 0; p < np; p++) {
                    if (player_card0_alive(&g, p)) alive_cards++;
                    else dead_cards++;
                    if (player_card1_alive(&g, p)) alive_cards++;
                    else dead_cards++;
                }
                if (dt + alive_cards + dead_cards != 15) {
                    fprintf(stderr, "FAIL: card total %d+%d+%d != 15 at seed=%llu step=%d phase=%d\n",
                            dt, alive_cards, dead_cards, (unsigned long long)seed, steps, phase);
                    exit(1);
                }
            }

            if (is_chance_node(&g)) {
                ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                int n = chance_outcomes(&g, out);
                if (n <= 0) break;
                apply_chance(&g, out[fuzz_rand() % n].outcome);
            } else {
                int a = random_legal_action(&g);
                if (a < 0) break;
                step_deterministic(&g, a);
            }
            steps++;
        }

        if (!is_done(&g)) continue;

        int ac = count_alive(&g);
        int w = get_winner(&g);

        if (ac == 1) {
            normal_games++;
            /* Single winner — standard case */
            if (!player_is_alive(&g, w)) {
                fprintf(stderr, "FAIL: winner %d dead (normal end) seed=%llu\n",
                        w, (unsigned long long)seed);
                exit(1);
            }
            for (int p = 0; p < np; p++) {
                if (p != w && player_is_alive(&g, p)) {
                    fprintf(stderr, "FAIL: non-winner %d alive (normal end) seed=%llu\n",
                            p, (unsigned long long)seed);
                    exit(1);
                }
            }
        } else {
            timeout_games++;
            /* Timeout — winner should be the strongest alive player */
            if (w < 0 || w >= np) {
                fprintf(stderr, "FAIL: invalid timeout winner %d seed=%llu\n",
                        w, (unsigned long long)seed);
                exit(1);
            }
            if (!player_is_alive(&g, w)) {
                fprintf(stderr, "FAIL: timeout winner %d dead seed=%llu\n",
                        w, (unsigned long long)seed);
                exit(1);
            }
        }
    }

    printf("PASS (normal=%d, timeout=%d)\n", normal_games, timeout_games);
}

/* ------------------------------------------------------------------ */
/* Fuzz 12: heuristic bot games complete without engine errors         */
/* ------------------------------------------------------------------ */

static void fuzz_heuristic_bot_games(void) {
    printf("fuzz_heuristic_bot_games ... ");
    int games_completed = 0;

    for (uint64_t seed = 1; seed <= 200; seed++) {
        for (int np = 2; np <= 6; np++) {
            Game g;
            game_init(&g, np, seed, seed + 13);

            int steps = 0;
            while (!is_done(&g) && steps < MAX_STEPS) {
                if (is_chance_node(&g)) {
                    ChanceOutcome out[MAX_CHANCE_OUTCOMES];
                    int n = chance_outcomes(&g, out);
                    if (n <= 0) break;
                    /* Pick first chance outcome */
                    apply_chance(&g, out[0].outcome);
                } else {
                    int action = heuristic_choose_action(&g);
                    uint32_t mask = get_valid_actions(&g);
                    if (!((mask >> action) & 1)) {
                        fprintf(stderr, "FAIL: heuristic returned invalid action %d "
                                "mask=0x%x seed=%llu np=%d step=%d\n",
                                action, mask, (unsigned long long)seed, np, steps);
                        exit(1);
                    }

                    step_with_rng(&g, action);
                }
                steps++;
            }

            if (is_done(&g)) {
                games_completed++;
                int w = get_winner(&g);
                if (w < 0 || w >= np || !player_is_alive(&g, w)) {
                    fprintf(stderr, "FAIL: bad winner %d seed=%llu np=%d\n",
                            w, (unsigned long long)seed, np);
                    exit(1);
                }
            }
        }
    }

    printf("PASS (%d games completed)\n", games_completed);
}

/* ------------------------------------------------------------------ */
/* Test: house rules — refund on challenge flag get/set                */
/* ------------------------------------------------------------------ */

static void test_refund_flag_getset(void) {
    printf("test_refund_flag_getset ... ");

    Game g;
    game_init(&g, 2, 1, 1);

    /* Default should be ON (official rules) */
    assert(game_get_refund_on_challenge(&g) == 1);

    /* Can set to OFF */
    game_set_refund_on_challenge(&g, 0);
    assert(game_get_refund_on_challenge(&g) == 0);

    /* Can set back to ON */
    game_set_refund_on_challenge(&g, 1);
    assert(game_get_refund_on_challenge(&g) == 1);

    /* Setting the flag doesn't corrupt game state — play a game after toggling */
    game_set_refund_on_challenge(&g, 0);
    game_set_refund_on_challenge(&g, 1);
    /* Game should still be in valid state */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(player_coins(&g, 0) == 1);
    assert(player_coins(&g, 1) == 2);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: assassinate challenge refund — flag ON (official rules)       */
/* ------------------------------------------------------------------ */

static void test_assassinate_challenge_refund_on(void) {
    printf("test_assassinate_challenge_refund_on ... ");

    Game g;
    game_init(&g, 2, 1, 1);

    /* Default is ON (official) — verify */
    assert(game_get_refund_on_challenge(&g) == 1);

    /* Force hands: P0 has Captain+Ambassador (no Assassin), P1 has Duke+Contessa */
    set_player_card0_type(&g, 0, CAPTAIN);
    set_player_card1_type(&g, 0, AMBASSADOR);
    set_player_card0_type(&g, 1, DUKE);
    set_player_card1_type(&g, 1, CONTESSA);

    /* Fix deck: dealt CAPTAIN, AMBASSADOR, DUKE, CONTESSA */
    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, ASSASSIN, 3);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, AMBASSADOR, 2);
    deck_set_count(&g, CONTESSA, 2);
    assert(deck_total(&g) == 11);

    /* Give P0 enough coins for assassination */
    set_player_coins(&g, 0, 3);

    int coins_before = player_coins(&g, 0);
    assert(coins_before == 3);

    /* P0 claims Assassin to assassinate P1 — BLUFF! P0 has no Assassin */
    step_deterministic(&g, ACT_ASSASSINATE_P0 + 1); /* target P1 */
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* 3 coins deducted immediately */
    assert(player_coins(&g, 0) == 0);

    /* P1 challenges */
    step_deterministic(&g, ACT_CHALLENGE);

    /* P0 doesn't have Assassin — challenge succeeds, P0 loses card */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 0);

    /* With refund ON: coins should be refunded already */
    assert(player_coins(&g, 0) == 3);

    /* P0 discards slot 0 */
    step_deterministic(&g, ACT_DISCARD_SLOT0);

    /* Turn should advance to P1 */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(get_turn_player(&g) == 1);

    /* Coins should still be 3 (refunded) */
    assert(player_coins(&g, 0) == 3);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: assassinate challenge NO refund — flag OFF (house rule)       */
/* ------------------------------------------------------------------ */

static void test_assassinate_challenge_refund_off(void) {
    printf("test_assassinate_challenge_refund_off ... ");

    Game g;
    game_init(&g, 2, 1, 1);

    /* Turn off refund (house rule: no refund) */
    game_set_refund_on_challenge(&g, 0);
    assert(game_get_refund_on_challenge(&g) == 0);

    /* Force hands: P0 has Captain+Ambassador (no Assassin), P1 has Duke+Contessa */
    set_player_card0_type(&g, 0, CAPTAIN);
    set_player_card1_type(&g, 0, AMBASSADOR);
    set_player_card0_type(&g, 1, DUKE);
    set_player_card1_type(&g, 1, CONTESSA);

    /* Fix deck */
    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, ASSASSIN, 3);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, AMBASSADOR, 2);
    deck_set_count(&g, CONTESSA, 2);
    assert(deck_total(&g) == 11);

    /* Give P0 enough coins for assassination */
    set_player_coins(&g, 0, 3);

    /* P0 claims Assassin to assassinate P1 — BLUFF! */
    step_deterministic(&g, ACT_ASSASSINATE_P0 + 1);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);
    assert(player_coins(&g, 0) == 0); /* deducted immediately */

    /* P1 challenges */
    step_deterministic(&g, ACT_CHALLENGE);

    /* P0 doesn't have Assassin — challenge succeeds, P0 loses card */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 0);

    /* With refund OFF: coins should NOT be refunded */
    assert(player_coins(&g, 0) == 0);

    /* P0 discards slot 0 */
    step_deterministic(&g, ACT_DISCARD_SLOT0);

    /* Turn advances, coins still 0 */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(player_coins(&g, 0) == 0);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: successful assassinate (unchallenged) — coins not refunded    */
/* ------------------------------------------------------------------ */

static void test_assassinate_unchallenged_no_refund(void) {
    printf("test_assassinate_unchallenged_no_refund ... ");

    Game g;
    game_init(&g, 2, 1, 1);

    /* Flag ON — but action goes through, so no refund */
    assert(game_get_refund_on_challenge(&g) == 1);

    /* Force hands: P0 has Assassin+Captain, P1 has Duke+Ambassador */
    set_player_card0_type(&g, 0, ASSASSIN);
    set_player_card1_type(&g, 0, CAPTAIN);
    set_player_card0_type(&g, 1, DUKE);
    set_player_card1_type(&g, 1, AMBASSADOR);

    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, ASSASSIN, 2);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, AMBASSADOR, 2);
    deck_set_count(&g, CONTESSA, 3);
    assert(deck_total(&g) == 11);

    set_player_coins(&g, 0, 3);

    /* P0 assassinates P1 */
    step_deterministic(&g, ACT_ASSASSINATE_P0 + 1);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);
    assert(player_coins(&g, 0) == 0);

    /* P1 passes (does not challenge) */
    step_deterministic(&g, ACT_PASS);

    /* Goes to block phase or resolve. P1 could block with Contessa.
       P1 doesn't have Contessa, but let's pass to resolve. */
    if (get_phase(&g) == PHASE_BLOCK) {
        step_deterministic(&g, ACT_PASS);
    }

    /* Resolve: P1 loses a card */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 1);

    /* Coins should still be 0 — assassination was paid, not refunded */
    assert(player_coins(&g, 0) == 0);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Test: non-assassinate challenge — tax bluff, no coin effect         */
/* ------------------------------------------------------------------ */

static void test_tax_challenge_no_refund_needed(void) {
    printf("test_tax_challenge_no_refund_needed ... ");

    Game g;
    game_init(&g, 2, 1, 1);
    assert(game_get_refund_on_challenge(&g) == 1);

    /* P0 has Captain+Ambassador (no Duke) */
    set_player_card0_type(&g, 0, CAPTAIN);
    set_player_card1_type(&g, 0, AMBASSADOR);
    set_player_card0_type(&g, 1, DUKE);
    set_player_card1_type(&g, 1, CONTESSA);

    deck_set_count(&g, DUKE, 2);
    deck_set_count(&g, ASSASSIN, 3);
    deck_set_count(&g, CAPTAIN, 2);
    deck_set_count(&g, AMBASSADOR, 2);
    deck_set_count(&g, CONTESSA, 2);

    int coins_before = player_coins(&g, 0);

    /* P0 claims Tax (Duke) — bluff */
    step_deterministic(&g, ACT_TAX);
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* Tax doesn't cost coins, so coins unchanged */
    assert(player_coins(&g, 0) == coins_before);

    /* P1 challenges */
    step_deterministic(&g, ACT_CHALLENGE);

    /* P0 loses card, coins unchanged (tax costs nothing) */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(player_coins(&g, 0) == coins_before);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Fuzz: games complete with refund flag ON (official rules)           */
/* ------------------------------------------------------------------ */

static void fuzz_refund_on_games(void) {
    printf("fuzz_refund_on_games ... ");

    int games_completed = 0;
    for (int seed = 1; seed <= 500; seed++) {
        Game g;
        game_init(&g, (seed % 5) + 2, (uint64_t)seed, (uint64_t)(seed + 1000));
        /* Default is ON */
        assert(game_get_refund_on_challenge(&g) == 1);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            assert(!is_chance_node(&g));
            int a = first_legal_action(&g);
            step_with_rng(&g, a);
            steps++;

            /* Coins should never go negative */
            for (int p = 0; p < get_num_players_ext(&g); p++) {
                assert(player_coins(&g, p) >= 0);
            }
        }
        assert(is_done(&g));
        games_completed++;
    }

    printf("PASS (%d games completed)\n", games_completed);
}

/* ------------------------------------------------------------------ */
/* Fuzz: games complete with refund flag OFF (house rule)              */
/* ------------------------------------------------------------------ */

static void fuzz_refund_off_games(void) {
    printf("fuzz_refund_off_games ... ");

    int games_completed = 0;
    for (int seed = 1; seed <= 500; seed++) {
        Game g;
        game_init(&g, (seed % 5) + 2, (uint64_t)seed, (uint64_t)(seed + 1000));
        game_set_refund_on_challenge(&g, 0);

        int steps = 0;
        while (!is_done(&g) && steps < MAX_STEPS) {
            assert(!is_chance_node(&g));
            int a = first_legal_action(&g);
            step_with_rng(&g, a);
            steps++;

            for (int p = 0; p < get_num_players_ext(&g); p++) {
                assert(player_coins(&g, p) >= 0);
            }
        }
        assert(is_done(&g));
        games_completed++;
    }

    printf("PASS (%d games completed)\n", games_completed);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Test: Assassinate -> Block Contessa -> Challenge block (blocker has card) */
/* ------------------------------------------------------------------ */

static void test_assassinate_block_challenge_block_stands(void) {
    printf("test_assassinate_block_challenge_block_stands ... ");

    /* 6-player game: player 0 assassinates player 3,
       player 3 blocks with Contessa (and HAS it),
       player 1 challenges the block — should lose. */
    Game g;
    game_init(&g, 6, 100, 100);

    /* Force known hands */
    set_player_card0_type(&g, 0, ASSASSIN);   /* Matt — has Assassin */
    set_player_card1_type(&g, 0, DUKE);
    set_player_card0_type(&g, 1, CAPTAIN);    /* Takumi — will challenge */
    set_player_card1_type(&g, 1, AMBASSADOR);
    set_player_card0_type(&g, 2, DUKE);       /* Lucia */
    set_player_card1_type(&g, 2, CAPTAIN);
    set_player_card0_type(&g, 3, CONTESSA);   /* Oscar — has Contessa */
    set_player_card1_type(&g, 3, DUKE);
    set_player_card0_type(&g, 4, AMBASSADOR); /* Silke */
    set_player_card1_type(&g, 4, ASSASSIN);
    set_player_card0_type(&g, 5, CAPTAIN);    /* Player 5 */
    set_player_card1_type(&g, 5, CONTESSA);

    /* Fix deck counts to be consistent */
    deck_set_count(&g, DUKE, 0);
    deck_set_count(&g, ASSASSIN, 1);
    deck_set_count(&g, CAPTAIN, 0);
    deck_set_count(&g, AMBASSADOR, 1);
    deck_set_count(&g, CONTESSA, 1);

    /* Verify Oscar is alive with 2 influence */
    assert(player_is_alive(&g, 3));
    assert(player_card0_alive(&g, 3) && player_card1_alive(&g, 3));

    /* Player 0 coins must be >= 3 for assassination */
    set_player_coins(&g, 0, 3);

    /* Step 1: Player 0 assassinates player 3 */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);
    assert(get_turn_player(&g) == 0);
    step_deterministic(&g, ACT_ASSASSINATE_P0 + 3);

    /* Should enter PHASE_CHALLENGE_ACTION */
    assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);

    /* Step 2: All other players pass the challenge window */
    /* Players cycle: 1, 2, 3, 4, 5 (skipping turn player 0) */
    for (int i = 0; i < 5; i++) {
        assert(get_phase(&g) == PHASE_CHALLENGE_ACTION);
        step_deterministic(&g, ACT_PASS);
    }

    /* Step 3: Should be in PHASE_BLOCK, Oscar (player 3) is active */
    assert(get_phase(&g) == PHASE_BLOCK);
    assert(get_active_player(&g) == 3);

    /* Oscar blocks with Contessa */
    step_deterministic(&g, ACT_BLOCK_CONTESSA);

    /* Step 4: PHASE_CHALLENGE_BLOCK — players can challenge Oscar's Contessa */
    assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK);

    /* Turn player (0) goes first in challenge cycle, then others skip blocker (3) */
    /* Player 0 passes */
    assert(get_active_player(&g) == 0);
    step_deterministic(&g, ACT_PASS);

    /* Player 1 (Takumi) challenges! */
    assert(get_active_player(&g) == 1);
    step_deterministic(&g, ACT_CHALLENGE);

    /* Oscar HAS Contessa => challenger (player 1) must lose a card */
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    assert(get_active_player(&g) == 1);  /* Takumi loses, NOT Oscar */

    /* Takumi discards slot 0 */
    step_deterministic(&g, ACT_DISCARD_SLOT0);

    /* Oscar should get a redraw (shuffle Contessa back, draw new) */
    assert(get_phase(&g) == PHASE_CHANCE_REDRAW);
    assert(get_active_player(&g) == 3);  /* Oscar gets redraw */

    /* Resolve the chance node */
    ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
    int n = chance_outcomes(&g, outcomes);
    assert(n > 0);
    apply_chance(&g, outcomes[0].outcome);

    /* Block stands => assassination cancelled, turn advances */
    assert(get_phase(&g) == PHASE_MAIN_ACTION);

    /* Oscar must still be alive with both cards */
    assert(player_is_alive(&g, 3));
    assert(player_card1_alive(&g, 3));  /* card1 was never touched */

    /* Takumi lost card 0 */
    assert(!player_card0_alive(&g, 1));
    assert(player_card1_alive(&g, 1));

    /* Player 0's coins were spent (3 coins for assassinate) but NOT refunded
       since the action was blocked (not challenge-cancelled) */
    assert(player_coins(&g, 0) == 0);

    printf("PASS\n");
}

/* ------------------------------------------------------------------ */
/* Rules audit / API contract tests                                    */
/* ------------------------------------------------------------------ */

/* Give each seat a fixed hand (both cards alive) and rebuild the deck. */
static void set_hands(Game *g, const int hands[][2]) {
    int np = get_num_players(g);
    int counts[5] = {3, 3, 3, 3, 3};
    for (int p = 0; p < np; p++) {
        set_player_card0_type(g, p, hands[p][0]);
        set_player_card1_type(g, p, hands[p][1]);
        set_player_card0_alive(g, p, 1);
        set_player_card1_alive(g, p, 1);
        counts[hands[p][0]]--;
        counts[hands[p][1]]--;
    }
    for (int c = 0; c < 5; c++) {
        assert(counts[c] >= 0);
        deck_set_count(g, c, counts[c]);
    }
}

static int must_step(Game *g, int action) {
    int rc = step_deterministic(g, action);
    if (rc != 0) {
        fprintf(stderr, "\nFAIL: action %d rejected (phase=%d active=%d mask=0x%x)\n",
                action, get_phase(g), get_active_player(g), get_valid_actions(g));
        exit(1);
    }
    return rc;
}

static void test_invalid_actions_rejected(void) {
    printf("test_invalid_actions_rejected ... ");
    Game g, before;
    game_init(&g, 3, 5, 6);
    before = g;

    /* Coup with 2 coins would wrap the 4-bit coin field. */
    assert(step_deterministic(&g, ACT_COUP_P0 + 1) == -1);
    assert(step_deterministic(&g, ACT_ASSASSINATE_P0 + 1) == -1);
    assert(step_deterministic(&g, ACT_STEAL_P0 + 0) == -1);   /* self */
    assert(step_deterministic(&g, ACT_STEAL_P0 + 4) == -1);   /* no such seat */
    assert(step_deterministic(&g, ACT_CHALLENGE) == -1);
    assert(step_deterministic(&g, ACT_DISCARD_SLOT2) == -1);
    assert(step_deterministic(&g, -1) == -1);
    assert(step_deterministic(&g, 32) == -1);
    assert(step_deterministic(&g, 1000) == -1);
    assert(step_with_rng(&g, 31) == -1);
    assert(memcmp(&g, &before, sizeof g) == 0);

    /* LOSE_CARD only accepts discards of living cards. */
    set_player_coins(&g, 0, 7);
    must_step(&g, ACT_COUP_P0 + 1);
    assert(get_phase(&g) == PHASE_LOSE_CARD);
    before = g;
    assert(step_deterministic(&g, ACT_PASS) == -1);
    assert(step_deterministic(&g, ACT_DISCARD_SLOT2) == -1);
    assert(memcmp(&g, &before, sizeof g) == 0);
    must_step(&g, ACT_DISCARD_SLOT0);
    must_step(&g, ACT_INCOME);                 /* p1 */
    must_step(&g, ACT_INCOME);                 /* p2 */
    set_player_coins(&g, 0, 7);
    must_step(&g, ACT_COUP_P0 + 1);
    before = g;
    assert(step_deterministic(&g, ACT_DISCARD_SLOT0) == -1); /* already dead */
    assert(memcmp(&g, &before, sizeof g) == 0);
    must_step(&g, ACT_DISCARD_SLOT1);
    assert(!player_is_alive(&g, 1));

    /* Chance nodes reject decisions and bad outcomes. */
    Game c;
    game_init(&c, 2, 0, 0);
    assert(is_chance_node(&c));
    before = c;
    assert(step_deterministic(&c, ACT_INCOME) == -1);
    assert(apply_chance(&c, 5) == -1);
    assert(apply_chance(&c, -1) == -1);
    assert(memcmp(&c, &before, sizeof c) == 0);
    deck_set_count(&c, DUKE, 0);
    assert(apply_chance(&c, DUKE) == -1);

    /* Decision nodes reject apply_chance. */
    before = g;
    assert(apply_chance(&g, DUKE) == -1);
    assert(memcmp(&g, &before, sizeof g) == 0);

    /* Finished games accept nothing and report an empty mask. */
    Game d;
    game_init(&d, 2, 9, 9);
    set_player_coins(&d, 0, 7);
    set_player_card1_alive(&d, 1, 0);
    must_step(&d, ACT_COUP_P0 + 1);
    must_step(&d, ACT_DISCARD_SLOT0);
    assert(is_done(&d) && get_winner(&d) == 0);
    assert(get_valid_actions(&d) == 0);
    before = d;
    assert(step_with_rng(&d, ACT_DISCARD_SLOT1) == -1);
    assert(memcmp(&d, &before, sizeof d) == 0);
    printf("PASS\n");
}

static void test_seedless_init_is_chance_deal(void) {
    printf("test_seedless_init_is_chance_deal ... ");
    for (int np = 2; np <= 6; np++) {
        Game g;
        game_init(&g, np, 0, 0);
        assert(get_phase(&g) == PHASE_DEAL);
        assert(!is_done(&g));             /* was wrongly "done" before */
        assert(get_active_player_ext(&g) == -1);
        assert(get_valid_actions(&g) == 0);
        /* Enumerate the deal manually: 2*np chance nodes. */
        int nodes = 0;
        while (is_chance_node(&g)) {
            ChanceOutcome out[MAX_CHANCE_OUTCOMES];
            int n = chance_outcomes(&g, out);
            assert(n > 0);
            assert(apply_chance(&g, out[n - 1].outcome) == 0);
            nodes++;
        }
        assert(nodes == 2 * np);
        assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 0);
        assert(deck_total(&g) == 15 - 2 * np);

        /* step_with_rng at a chance node resolves the whole deal. */
        Game h;
        game_init(&h, np, 0, 0);
        assert(step_with_rng(&h, 0) == -1);
        assert(get_phase(&h) == PHASE_MAIN_ACTION);
        assert(deck_total(&h) == 15 - 2 * np);
        for (int p = 0; p < np; p++) assert(player_alive_cards(&h, p) == 2);
    }
    /* Seeded deal is a pure function of deal_seed; proc_seed only seeds g->rng. */
    Game a, b;
    game_init(&a, 4, 77, 1);
    game_init(&b, 4, 77, 2);
    assert(memcmp(a.players, b.players, sizeof a.players) == 0 && a.deck == b.deck);
    /* deal_seed == 0 is a valid seed when proc_seed != 0. */
    game_init(&a, 4, 0, 1);
    assert(get_phase(&a) == PHASE_MAIN_ACTION);
    printf("PASS\n");
}

/* Target challenges a real Assassin, loses a card, then may still block or
 * lose a second card to the assassination (official "double loss"). */
static void test_assassinate_double_loss_via_challenge(void) {
    printf("test_assassinate_double_loss_via_challenge ... ");
    const int hands[2][2] = {{ASSASSIN, DUKE}, {CAPTAIN, AMBASSADOR}};
    Game g;
    game_init(&g, 2, 3, 3);
    set_hands(&g, hands);
    set_player_coins(&g, 0, 3);
    must_step(&g, ACT_ASSASSINATE_P0 + 1);
    assert(player_coins(&g, 0) == 0);
    must_step(&g, ACT_CHALLENGE);
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT0);
    assert(get_phase(&g) == PHASE_CHANCE_REDRAW && get_active_player(&g) == 0);
    assert(apply_chance(&g, ASSASSIN) == 0);  /* redraw (Assassin went back) */
    /* Target, still alive, gets the block window. */
    assert(get_phase(&g) == PHASE_BLOCK && get_active_player(&g) == 1);
    assert(get_valid_actions(&g) == ((1u << ACT_PASS) | (1u << ACT_BLOCK_CONTESSA)));
    must_step(&g, ACT_PASS);
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT1);
    assert(is_done(&g) && get_winner(&g) == 0);
    assert(player_coins(&g, 0) == 0); /* no refund: assassination succeeded */
    printf("PASS\n");
}

/* Target bluffs Contessa, gets challenged: loses a card for the bluff and
 * then another to the assassination. */
static void test_assassinate_double_loss_via_bluff_block(void) {
    printf("test_assassinate_double_loss_via_bluff_block ... ");
    const int hands[3][2] = {{ASSASSIN, DUKE}, {CAPTAIN, AMBASSADOR}, {DUKE, CAPTAIN}};
    Game g;
    game_init(&g, 3, 3, 3);
    set_hands(&g, hands);
    set_player_coins(&g, 0, 4);
    must_step(&g, ACT_ASSASSINATE_P0 + 1);
    must_step(&g, ACT_PASS); /* p1 */
    must_step(&g, ACT_PASS); /* p2 */
    assert(get_phase(&g) == PHASE_BLOCK && get_active_player(&g) == 1);
    must_step(&g, ACT_BLOCK_CONTESSA);
    assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK);
    assert(get_active_player(&g) == 0); /* turn player may challenge first */
    must_step(&g, ACT_PASS);
    assert(get_active_player(&g) == 2); /* ... and so may a bystander */
    must_step(&g, ACT_CHALLENGE);
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT0);
    /* Block failed: assassination resolves against the same target. */
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT1);
    assert(!player_is_alive(&g, 1));
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 2);
    assert(player_coins(&g, 0) == 1);
    printf("PASS\n");
}

/* Blocked assassination: coins stay spent even though nothing happened. */
static void test_assassinate_blocked_coins_spent(void) {
    printf("test_assassinate_blocked_coins_spent ... ");
    const int hands[2][2] = {{ASSASSIN, DUKE}, {CONTESSA, AMBASSADOR}};
    Game g;
    game_init(&g, 2, 3, 3);
    set_hands(&g, hands);
    set_player_coins(&g, 0, 3);
    must_step(&g, ACT_ASSASSINATE_P0 + 1);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_BLOCK_CONTESSA);
    must_step(&g, ACT_PASS);
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 1);
    assert(player_coins(&g, 0) == 0);
    assert(player_alive_cards(&g, 1) == 2);
    printf("PASS\n");
}

static void test_exchange_single_influence(void) {
    printf("test_exchange_single_influence ... ");
    const int hands[2][2] = {{AMBASSADOR, DUKE}, {CAPTAIN, CONTESSA}};
    Game g;
    game_init(&g, 2, 3, 3);
    set_hands(&g, hands);
    set_player_card1_alive(&g, 0, 0); /* p0 has only the Ambassador left */
    must_step(&g, ACT_EXCHANGE);
    must_step(&g, ACT_PASS);
    assert(get_phase(&g) == PHASE_CHANCE_EXCHANGE);
    assert(apply_chance(&g, DUKE) == 0);
    assert(apply_chance(&g, CAPTAIN) == 0);
    assert(get_phase(&g) == PHASE_EXCHANGE_DISCARD);
    /* 3 cards to choose from (living hand card + 2 drawn), keep 1. */
    uint32_t first = (1u << ACT_DISCARD_SLOT0) | (1u << ACT_DISCARD_SLOT2);
    assert(get_valid_actions(&g) == first);
    must_step(&g, ACT_DISCARD_SLOT0);   /* give back the Ambassador */
    assert(get_valid_actions(&g) == ((1u << ACT_DISCARD_SLOT2) | (1u << ACT_DISCARD_SLOT3)));
    must_step(&g, ACT_DISCARD_SLOT3);   /* give back the Captain, keep Duke */
    assert(player_card0_alive(&g, 0) && player_card0_type(&g, 0) == DUKE);
    assert(!player_card1_alive(&g, 0) && player_card1_type(&g, 0) == DUKE); /* dead card untouched */
    assert(deck_count(&g, AMBASSADOR) == 3 && deck_count(&g, CAPTAIN) == 2);
    assert(deck_total(&g) == 11);
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 1);
    printf("PASS\n");
}

static void test_steal_from_poor_players(void) {
    printf("test_steal_from_poor_players ... ");
    const int hands[3][2] = {{CAPTAIN, DUKE}, {CAPTAIN, CONTESSA}, {DUKE, AMBASSADOR}};
    Game g;
    game_init(&g, 3, 3, 3);
    set_hands(&g, hands);
    set_player_coins(&g, 1, 1);
    set_player_coins(&g, 2, 0);
    /* Steal from a 1-coin player takes 1. */
    must_step(&g, ACT_STEAL_P0 + 1);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    assert(get_phase(&g) == PHASE_BLOCK && get_active_player(&g) == 1);
    must_step(&g, ACT_PASS);
    assert(player_coins(&g, 0) == 3 && player_coins(&g, 1) == 0);
    /* Steal from a 0-coin player is legal and takes nothing. */
    assert(get_turn_player(&g) == 1);
    must_step(&g, ACT_STEAL_P0 + 2);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    assert(player_coins(&g, 1) == 0 && player_coins(&g, 2) == 0);
    /* Only the target may block a steal; Captain and Ambassador both work. */
    assert(get_turn_player(&g) == 2);
    must_step(&g, ACT_STEAL_P0 + 0);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    assert(get_phase(&g) == PHASE_BLOCK && get_active_player(&g) == 0);
    assert(get_valid_actions(&g) == ((1u << ACT_PASS) | (1u << ACT_BLOCK_CAPTAIN) |
                                     (1u << ACT_BLOCK_AMBASSADOR)));
    must_step(&g, ACT_BLOCK_AMBASSADOR); /* bluff, but nobody challenges */
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    assert(player_coins(&g, 0) == 3 && player_coins(&g, 2) == 0);
    assert(get_turn_player(&g) == 0);
    printf("PASS\n");
}

/* Foreign aid: any opponent may block with Duke; any other player (not just
 * the actor) may challenge that block. */
static void test_foreign_aid_block_challenged_by_bystander(void) {
    printf("test_foreign_aid_block_challenged_by_bystander ... ");
    const int hands[4][2] = {{CAPTAIN, CAPTAIN}, {AMBASSADOR, CONTESSA},
                             {DUKE, ASSASSIN}, {DUKE, DUKE}};
    Game g;
    game_init(&g, 4, 3, 3);
    set_hands(&g, hands);
    must_step(&g, ACT_FOREIGN_AID);
    assert(get_phase(&g) == PHASE_BLOCK && get_active_player(&g) == 1);
    must_step(&g, ACT_BLOCK_DUKE); /* p1 bluffs Duke */
    assert(get_phase(&g) == PHASE_CHALLENGE_BLOCK && get_active_player(&g) == 0);
    must_step(&g, ACT_PASS);
    assert(get_active_player(&g) == 2);
    must_step(&g, ACT_PASS);
    assert(get_active_player(&g) == 3);
    must_step(&g, ACT_CHALLENGE); /* p3 holds all three Dukes' worth of info */
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT1);
    /* Block failed: foreign aid resolves. */
    assert(player_coins(&g, 0) == 4);
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 1);

    /* A true block that is challenged: challenger loses, blocker redraws,
     * the action stays blocked. */
    must_step(&g, ACT_FOREIGN_AID);                     /* p1 */
    assert(get_active_player(&g) == 2);
    must_step(&g, ACT_BLOCK_DUKE);                      /* p2 really has Duke */
    assert(get_active_player(&g) == 1);
    must_step(&g, ACT_CHALLENGE);
    assert(get_phase(&g) == PHASE_LOSE_CARD && get_active_player(&g) == 1);
    must_step(&g, ACT_DISCARD_SLOT0);
    assert(!player_is_alive(&g, 1));
    assert(get_phase(&g) == PHASE_CHANCE_REDRAW && get_active_player(&g) == 2);
    assert(apply_chance(&g, DUKE) == 0);
    assert(player_coins(&g, 1) == 2);
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 2);
    printf("PASS\n");
}

static void test_forced_coup_and_max_coins(void) {
    printf("test_forced_coup_and_max_coins ... ");
    Game g;
    game_init(&g, 3, 3, 3);
    set_player_coins(&g, 0, 10);
    uint32_t m = get_valid_actions(&g);
    assert(m == ((1u << (ACT_COUP_P0 + 1)) | (1u << (ACT_COUP_P0 + 2))));
    set_player_coins(&g, 0, 9);
    m = get_valid_actions(&g);
    assert(m & (1u << ACT_TAX));
    must_step(&g, ACT_TAX);
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_PASS);
    if (is_chance_node(&g)) { /* not reached: unchallenged tax resolves */ assert(0); }
    assert(player_coins(&g, 0) == 12);   /* max reachable */
    printf("PASS\n");
}

static void test_max_turns_tiebreak(void) {
    printf("test_max_turns_tiebreak ... ");
    Game g;
    game_init(&g, 3, 3, 3);
    g.turn_count = MAX_TURNS - 1;
    set_player_card0_alive(&g, 0, 0);
    set_player_coins(&g, 1, 5);
    set_player_coins(&g, 2, 5);
    must_step(&g, ACT_INCOME);
    assert(is_done(&g));
    assert(get_valid_actions(&g) == 0);
    assert(get_winner(&g) == 1); /* 2 cards beats 1; equal coins -> lowest seat */
    set_player_coins(&g, 2, 6);
    assert(get_winner(&g) == 2);
    printf("PASS\n");
}

/* A steal target who dies by losing a challenge still has coins taken. */
static void test_steal_from_target_killed_by_challenge(void) {
    printf("test_steal_from_target_killed_by_challenge ... ");
    const int hands[3][2] = {{CAPTAIN, DUKE}, {DUKE, AMBASSADOR}, {DUKE, CONTESSA}};
    Game g;
    game_init(&g, 3, 3, 3);
    set_hands(&g, hands);
    set_player_card1_alive(&g, 1, 0);
    must_step(&g, ACT_STEAL_P0 + 1);
    must_step(&g, ACT_CHALLENGE);   /* p1 challenges and is wrong */
    must_step(&g, ACT_DISCARD_SLOT0);
    assert(!player_is_alive(&g, 1));
    assert(apply_chance(&g, CAPTAIN) == 0);
    assert(get_phase(&g) == PHASE_MAIN_ACTION && get_turn_player(&g) == 2);
    /* Eliminated target leaves with their coins: nothing is stolen. */
    assert(player_coins(&g, 0) == 2 && player_coins(&g, 1) == 2);
    printf("PASS\n");
}

/* Random play through every phase: PHASE_RESOLVE never surfaces, chance
 * nodes never surface from step_with_rng, and deterministic replays match. */
static void fuzz_resolve_never_observable(void) {
    printf("fuzz_resolve_never_observable ... ");
    long steps = 0;
    for (uint64_t seed = 1; seed <= 2000; seed++) {
        Game g;
        int np = 2 + (int)(seed % 5);
        game_init(&g, np, seed, seed * 31);
        while (!is_done(&g)) {
            assert(get_phase(&g) != PHASE_RESOLVE);
            assert(!is_chance_node(&g));
            uint32_t m = get_valid_actions(&g);
            assert(m != 0);
            int a = random_legal_action(&g);
            assert(step_with_rng(&g, a) == 0);
            steps++;
        }
        assert(get_phase(&g) != PHASE_RESOLVE);
    }
    printf("PASS (%ld steps)\n", steps);
}

/* Throw arbitrary (mostly invalid) actions and chance outcomes at the engine:
 * it must accept exactly the masked ones and never corrupt state. */
static void fuzz_garbage_actions(void) {
    printf("fuzz_garbage_actions ... ");
    long accepted = 0, rejected = 0;
    for (uint64_t seed = 1; seed <= 1000; seed++) {
        fuzz_rng_state = (uint32_t)seed * 2654435761u;
        Game g;
        int np = 2 + (int)(seed % 5);
        game_init(&g, np, seed % 3 ? seed : 0, seed % 3 ? seed + 1 : 0);
        for (int step = 0; step < 4000 && !is_done(&g); step++) {
            Game before = g;
            if (is_chance_node(&g)) {
                int o = (int)(fuzz_rand() % 8) - 1;
                int ok = o >= 0 && o < 5 && deck_count(&g, o) > 0;
                int rc = apply_chance(&g, o);
                assert(rc == (ok ? 0 : -1));
                if (rc) assert(memcmp(&g, &before, sizeof g) == 0);
            } else {
                int a = (int)(fuzz_rand() % 40) - 4;
                uint32_t m = get_valid_actions(&g);
                int ok = a >= 0 && a < 32 && ((m >> a) & 1);
                int rc = step_deterministic(&g, a);
                assert(rc == (ok ? 0 : -1));
                if (rc) { assert(memcmp(&g, &before, sizeof g) == 0); rejected++; }
                else accepted++;
            }
            assert(total_cards_in_game(&g) + 0 <= 15);
            for (int p = 0; p < np; p++) assert(player_coins(&g, p) <= 12);
        }
    }
    printf("PASS (%ld accepted, %ld rejected)\n", accepted, rejected);
}

/* ---- Heuristic ladder ---- */

static int claim_role(int phase, int action) {
    if (phase == PHASE_MAIN_ACTION) {
        if (action == ACT_TAX) return DUKE;
        if (action == ACT_EXCHANGE) return AMBASSADOR;
        if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6) return CAPTAIN;
        if (action >= ACT_ASSASSINATE_P0 && action < ACT_ASSASSINATE_P0 + 6) return ASSASSIN;
    }
    if (action == ACT_BLOCK_DUKE) return DUKE;
    if (action == ACT_BLOCK_CAPTAIN) return CAPTAIN;
    if (action == ACT_BLOCK_AMBASSADOR) return AMBASSADOR;
    if (action == ACT_BLOCK_CONTESSA) return CONTESSA;
    return -1;
}

static void fuzz_heuristic_levels_honest(void) {
    printf("fuzz_heuristic_levels_honest ... ");
    long claims = 0, challenges = 0;
    for (uint64_t seed = 1; seed <= 1000; seed++) {
        for (int level = HEURISTIC_HONEST; level <= HEURISTIC_COUNTING; level++) {
            Game g;
            int np = 2 + (int)(seed % 5);
            game_init(&g, np, seed, seed + 5);
            while (!is_done(&g)) {
                int ap = get_active_player(&g);
                int phase = get_phase(&g);
                int a = heuristic_choose_action_level(&g, level);
                assert((get_valid_actions(&g) >> a) & 1);
                int role = claim_role(phase, a);
                if (role >= 0) {
                    assert(player_has_card(&g, ap, role)); /* never bluffs */
                    claims++;
                }
                if (a == ACT_CHALLENGE) {
                    assert(level == HEURISTIC_COUNTING);
                    challenges++;
                }
                assert(step_with_rng(&g, a) == 0);
            }
        }
    }
    printf("PASS (%ld honest claims, %ld challenges)\n", claims, challenges);
}

/* Counting bot challenges a claim it can prove false. */
static void test_counting_challenges_impossible_claim(void) {
    printf("test_counting_challenges_impossible_claim ... ");
    const int hands[3][2] = {{CAPTAIN, AMBASSADOR}, {DUKE, DUKE}, {DUKE, CONTESSA}};
    Game g;
    game_init(&g, 3, 3, 3);
    set_hands(&g, hands);
    set_player_card0_alive(&g, 2, 0);          /* p2's Duke is revealed */
    must_step(&g, ACT_TAX);                    /* p0 claims Duke */
    assert(get_active_player(&g) == 1);        /* p1 holds 2, 1 revealed: all 3 known */
    assert(heuristic_choose_action_level(&g, HEURISTIC_HONEST) == ACT_PASS);
    assert(heuristic_choose_action_level(&g, HEURISTIC_COUNTING) == ACT_CHALLENGE);
    must_step(&g, ACT_PASS);
    assert(get_active_player(&g) == 2);        /* p2 only knows 1 Duke */
    assert(heuristic_choose_action_level(&g, HEURISTIC_COUNTING) == ACT_PASS);

    /* Last-card assassination target without Contessa challenges. */
    const int h2[2][2] = {{CAPTAIN, DUKE}, {AMBASSADOR, DUKE}};
    game_init(&g, 2, 3, 3);
    set_hands(&g, h2);
    set_player_card1_alive(&g, 1, 0);
    set_player_coins(&g, 0, 3);
    must_step(&g, ACT_ASSASSINATE_P0 + 1);
    assert(heuristic_choose_action_level(&g, HEURISTIC_COUNTING) == ACT_CHALLENGE);
    assert(heuristic_choose_action_level(&g, HEURISTIC_HONEST) == ACT_PASS);
    printf("PASS\n");
}

/* Honest bots block only with cards they hold (previously they always
 * blocked, i.e. bluffed constantly). */
static void test_honest_blocks_only_with_card(void) {
    printf("test_honest_blocks_only_with_card ... ");
    const int hands[2][2] = {{DUKE, CAPTAIN}, {ASSASSIN, AMBASSADOR}};
    Game g;
    game_init(&g, 2, 3, 3);
    set_hands(&g, hands);
    must_step(&g, ACT_FOREIGN_AID);
    assert(heuristic_choose_action(&g) == ACT_PASS);       /* no Duke */
    must_step(&g, ACT_PASS);
    must_step(&g, ACT_STEAL_P0 + 0);                       /* p1 steals from p0 */
    must_step(&g, ACT_PASS);
    assert(heuristic_choose_action(&g) == ACT_BLOCK_CAPTAIN);
    printf("PASS\n");
}

/* Exchange discard keeps the two most valuable cards even when the two worst
 * are both hand cards (the old greedy pick discarded a drawn Duke here). */
static void test_heuristic_exchange_keeps_best(void) {
    printf("test_heuristic_exchange_keeps_best ... ");
    const int hands[2][2] = {{AMBASSADOR, CONTESSA}, {CAPTAIN, ASSASSIN}};
    Game g;
    game_init(&g, 2, 3, 3);
    set_hands(&g, hands);
    must_step(&g, ACT_EXCHANGE);
    must_step(&g, ACT_PASS);
    assert(apply_chance(&g, DUKE) == 0);
    assert(apply_chance(&g, DUKE) == 0);
    must_step(&g, heuristic_choose_action(&g));
    must_step(&g, heuristic_choose_action(&g));
    assert(player_card0_type(&g, 0) == DUKE && player_card1_type(&g, 0) == DUKE);
    printf("PASS\n");
}

/* Win rate of seat-rotated `a` vs `b` over n 2-player games. */
static double ladder_winrate(int a, int b, int n) {
    int wins = 0;
    Xoshiro256 rng;
    xoshiro256_seed(&rng, 1234);
    for (int i = 0; i < n; i++) {
        Game g;
        game_init(&g, 2, (uint64_t)i + 1, (uint64_t)i + 77);
        int a_seat = i & 1;
        while (!is_done(&g)) {
            int lvl = get_active_player(&g) == a_seat ? a : b;
            step_with_rng(&g, heuristic_choose_action_rng(&g, lvl, &rng));
        }
        wins += get_winner(&g) == a_seat;
    }
    return (double)wins / n;
}

static void test_heuristic_ladder_ordering(void) {
    printf("test_heuristic_ladder_ordering ... ");
    /* Counting only differs from honest when the opponent bluffs, so it ties
     * honest head-to-head and should do better against a (bluffing) random
     * player. */
    double h_vs_r = ladder_winrate(HEURISTIC_HONEST, HEURISTIC_RANDOM, 4000);
    double c_vs_r = ladder_winrate(HEURISTIC_COUNTING, HEURISTIC_RANDOM, 4000);
    double c_vs_h = ladder_winrate(HEURISTIC_COUNTING, HEURISTIC_HONEST, 4000);
    printf("(honest-random %.3f, counting-random %.3f, counting-honest %.3f) ",
           h_vs_r, c_vs_r, c_vs_h);
    assert(h_vs_r > 0.8);
    assert(c_vs_r > h_vs_r);
    assert(c_vs_h > 0.45);
    printf("PASS\n");
}

int main(void) {
    printf("=== Coup Engine Tests ===\n\n");

    test_deterministic_deal();
    test_full_game_rollout();
    test_mask_correctness();
    test_challenge_success();
    test_challenge_failure();
    test_block_resolution();
    test_ambassador_exchange();
    test_player_elimination_cycling();
    test_2player_completion();
    test_6player_completion();
    test_foreign_aid_block_cycling();
    test_chance_node_enumeration();
    test_deterministic_game();
    test_card_conservation();
    test_exchange_discard_slot3_first();
    test_exchange_discard_no_deadlock();
    test_refund_flag_getset();
    test_assassinate_challenge_refund_on();
    test_assassinate_challenge_refund_off();
    test_assassinate_unchallenged_no_refund();
    test_tax_challenge_no_refund_needed();
    test_assassinate_block_challenge_block_stands();
    test_invalid_actions_rejected();
    test_seedless_init_is_chance_deal();
    test_assassinate_double_loss_via_challenge();
    test_assassinate_double_loss_via_bluff_block();
    test_assassinate_blocked_coins_spent();
    test_exchange_single_influence();
    test_steal_from_poor_players();
    test_foreign_aid_block_challenged_by_bystander();
    test_forced_coup_and_max_coins();
    test_max_turns_tiebreak();
    test_steal_from_target_killed_by_challenge();
    test_counting_challenges_impossible_claim();
    test_honest_blocks_only_with_card();
    test_heuristic_exchange_keeps_best();
    test_heuristic_ladder_ordering();

    printf("\n--- Fuzz / stress tests ---\n\n");
    fuzz_valid_actions_nonempty();
    fuzz_card_conservation();
    fuzz_dead_player_never_active();
    fuzz_coin_bounds();
    fuzz_chance_probs_sum();
    fuzz_exactly_one_winner();
    fuzz_forced_coup();
    fuzz_step_with_rng();
    fuzz_no_info_leak();
    fuzz_game_end_invariants();
    fuzz_heuristic_bot_games();
    fuzz_refund_on_games();
    fuzz_refund_off_games();
    fuzz_resolve_never_observable();
    fuzz_garbage_actions();
    fuzz_heuristic_levels_honest();

    printf("\n=== All tests passed ===\n");
    return 0;
}
