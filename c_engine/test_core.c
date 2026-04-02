/*
 * test_core.c — Comprehensive tests for the Coup C engine.
 *
 * Build:
 *   cc -O2 -std=c11 -o test_core c_engine/test_core.c c_engine/coup_core.c -I c_engine/ -lm
 * Run:
 *   ./test_core
 */

#include "coup_core.h"
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

    /* Each player starts with 2 coins */
    assert(player_coins(&g, 0) == 2);
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
        int n = chance_outcomes(&g, out);
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
        int n = chance_outcomes(&g, out);
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
                if (c < 0 || c > 15) {
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

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

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

    printf("\n--- Fuzz / stress tests ---\n\n");
    fuzz_valid_actions_nonempty();
    fuzz_card_conservation();
    fuzz_dead_player_never_active();
    fuzz_coin_bounds();
    fuzz_chance_probs_sum();
    fuzz_exactly_one_winner();
    fuzz_forced_coup();
    fuzz_step_with_rng();

    printf("\n=== All tests passed ===\n");
    return 0;
}
