/*
 * test_text_render.c — Tests for the LLM text renderer.
 *
 * Build:
 *   cc -O2 -std=c11 -o test_text_render c_engine/test_text_render.c \
 *      c_engine/text_render.c c_engine/coup_core.c -I c_engine/ -lm
 * Run:
 *   ./test_text_render
 */

#include "text_render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BUF_SIZE 8192

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int first_legal_action(const Game *g) {
    uint32_t mask = get_valid_actions(g);
    assert(mask != 0);
    return __builtin_ctz(mask);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_gamelog_helpers(void) {
    printf("test_gamelog_helpers ... ");

    GameLog log;
    gamelog_init(&log);
    assert(log.len == 0);

    log_action(&log, 0, ACT_TAX, DUKE);
    assert(log.len == 1);
    assert(log.events[0].type == EVENT_ACTION);
    assert(log.events[0].actor == 0);
    assert(log.events[0].action == ACT_TAX);
    assert(log.events[0].role_claimed == DUKE);
    assert(log.events[0].target == NO_VAL);

    log_action(&log, 1, ACT_STEAL_P0 + 3, CAPTAIN);
    assert(log.len == 2);
    assert(log.events[1].target == 3);

    log_challenge(&log, 2, 1);
    assert(log.len == 3);
    assert(log.events[2].type == EVENT_CHALLENGE);
    assert(log.events[2].actor == 2);
    assert(log.events[2].target == 1);

    log_challenge_resolve(&log, 1, CAPTAIN, EVENT_OUTCOME_FAIL | EVENT_OUTCOME_SHUFFLED);
    assert(log.len == 4);
    assert(log.events[3].type == EVENT_CHALLENGE_RESOLVE);
    assert(log.events[3].card_revealed == CAPTAIN);
    assert(log.events[3].outcome == (EVENT_OUTCOME_FAIL | EVENT_OUTCOME_SHUFFLED));

    log_block(&log, 3, AMBASSADOR);
    assert(log.len == 5);
    assert(log.events[4].type == EVENT_BLOCK);
    assert(log.events[4].role_claimed == AMBASSADOR);

    log_pass(&log, 0);
    assert(log.len == 6);
    assert(log.events[5].type == EVENT_PASS);

    log_lose_card(&log, 2, DUKE);
    assert(log.len == 7);
    assert(log.events[6].type == EVENT_LOSE_CARD);
    assert(log.events[6].card_revealed == DUKE);

    log_exchange(&log, 0);
    assert(log.len == 8);
    assert(log.events[7].type == EVENT_EXCHANGE);

    log_eliminated(&log, 2);
    assert(log.len == 9);
    assert(log.events[8].type == EVENT_ELIMINATED);

    log_game_over(&log, 1);
    assert(log.len == 10);
    assert(log.events[9].type == EVENT_GAME_OVER);
    assert(log.events[9].actor == 1);

    printf("PASS\n");
}

static void test_main_action_render(void) {
    printf("test_main_action_render ... ");

    Game g;
    game_init(&g, 4, 42, 99);
    assert(get_phase(&g) == PHASE_MAIN_ACTION);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    TextActionMap map;
    int active = get_active_player_ext(&g);

    render_text(&g, active, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);

    /* Should contain player identity */
    char expected[64];
    snprintf(expected, sizeof(expected), "You are Player %d.", active);
    assert(strstr(buf, expected) != NULL);

    /* Should contain table info for other players */
    assert(strstr(buf, "Table:") != NULL);

    /* Should have actions since it's this player's turn */
    assert(map.num_options > 0);
    assert(strstr(buf, "Income") != NULL);
    assert(strstr(buf, "Foreign Aid") != NULL);

    /* Action map round-trip */
    uint32_t mask = get_valid_actions(&g);
    for (int i = 1; i <= map.num_options; i++) {
        int flat = parse_text_action(&map, i);
        assert(flat >= 0 && flat < 32);
        assert((mask >> flat) & 1);
    }

    printf("PASS\n");
}

static void test_not_active_player(void) {
    printf("test_not_active_player ... ");

    Game g;
    game_init(&g, 4, 42, 99);

    int active = get_active_player_ext(&g);
    int other = (active + 1) % 4;

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    TextActionMap map;
    render_text(&g, other, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);

    /* Non-active player should get no action options */
    assert(map.num_options == 0);

    /* But should still see their own identity */
    char expected[64];
    snprintf(expected, sizeof(expected), "You are Player %d.", other);
    assert(strstr(buf, expected) != NULL);

    printf("PASS\n");
}

static void test_info_mode_player(void) {
    printf("test_info_mode_player ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);

    /* Should NOT contain hidden info markers or deck info */
    assert(strstr(buf, "[hidden:") == NULL);
    assert(strstr(buf, "Deck") == NULL);

    printf("PASS\n");
}

static void test_info_mode_perfect(void) {
    printf("test_info_mode_perfect ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PERFECT, buf, sizeof(buf), NULL);

    /* Should contain hidden card info and deck */
    assert(strstr(buf, "[hidden:") != NULL);
    assert(strstr(buf, "Deck") != NULL);

    printf("PASS\n");
}

static void test_action_map_roundtrip(void) {
    printf("test_action_map_roundtrip ... ");

    /* Play a few steps, checking action map at each decision point */
    Game g;
    game_init(&g, 4, 123, 456);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    int steps = 0;

    while (!is_done(&g) && steps < 200) {
        assert(!is_chance_node(&g));

        int active = get_active_player_ext(&g);
        TextActionMap map;
        render_text(&g, active, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);

        /* Every option must map to a valid action */
        uint32_t mask = get_valid_actions(&g);
        assert(map.num_options > 0);
        for (int i = 1; i <= map.num_options; i++) {
            int flat = parse_text_action(&map, i);
            assert(flat >= 0 && flat < 32);
            assert((mask >> flat) & 1);
        }

        /* Out-of-range returns -1 */
        assert(parse_text_action(&map, 0) == -1);
        assert(parse_text_action(&map, map.num_options + 1) == -1);

        /* Count set bits in mask — should equal num_options */
        int count = 0;
        for (int b = 0; b < 32; b++) {
            if ((mask >> b) & 1) count++;
        }
        assert(map.num_options == count);

        int a = first_legal_action(&g);
        step_with_rng(&g, a);
        steps++;
    }

    printf("PASS (%d steps)\n", steps);
}

static void test_buffer_truncation(void) {
    printf("test_buffer_truncation ... ");

    Game g;
    game_init(&g, 6, 42, 99);

    GameLog log;
    gamelog_init(&log);
    /* Add some history to make output longer */
    log_action(&log, 0, ACT_TAX, DUKE);
    log_pass(&log, 1);
    log_pass(&log, 2);
    log_action(&log, 1, ACT_FOREIGN_AID, -1);
    log_pass(&log, 0);

    /* Render into a tiny buffer */
    char buf[64];
    TextActionMap map;
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);

    /* Must be null-terminated and not overflow */
    assert(buf[63] == '\0');
    assert(strlen(buf) < 64);

    /* Render into size=1 buffer */
    char tiny[1];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, tiny, 1, NULL);
    assert(tiny[0] == '\0');

    printf("PASS\n");
}

static void test_eliminated_player(void) {
    printf("test_eliminated_player ... ");

    Game g;
    game_init(&g, 3, 42, 99);

    /* Kill player 2: set both cards dead */
    set_player_card0_alive(&g, 2, 0);
    set_player_card1_alive(&g, 2, 0);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);

    assert(strstr(buf, "ELIMINATED") != NULL);

    printf("PASS\n");
}

static void test_forced_coup(void) {
    printf("test_forced_coup ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    /* Give the active player 10 coins */
    int active = get_active_player_ext(&g);
    set_player_coins(&g, active, 10);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    TextActionMap map;
    render_text(&g, active, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);

    assert(strstr(buf, "must Coup") != NULL);

    /* All options should be coup actions */
    for (int i = 1; i <= map.num_options; i++) {
        int flat = parse_text_action(&map, i);
        assert(flat >= ACT_COUP_P0 && flat < ACT_COUP_P0 + 6);
    }

    printf("PASS\n");
}

static void test_history_rendering(void) {
    printf("test_history_rendering ... ");

    Game g;
    game_init(&g, 4, 42, 99);

    GameLog log;
    gamelog_init(&log);

    /* Simulate: Player 1 claims Duke for Tax, no one challenges */
    log_action(&log, 1, ACT_TAX, DUKE);
    log_pass(&log, 0);
    log_pass(&log, 2);
    log_pass(&log, 3);

    /* Simulate: Player 2 tries to Steal from Player 0, gets challenged */
    log_action(&log, 2, ACT_STEAL_P0, CAPTAIN);
    log_challenge(&log, 0, 2);
    log_challenge_resolve(&log, 2, CAPTAIN, EVENT_OUTCOME_FAIL | EVENT_OUTCOME_SHUFFLED);
    log_lose_card(&log, 0, DUKE);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);

    /* Check history section exists */
    assert(strstr(buf, "History:") != NULL);
    assert(strstr(buf, "Tax") != NULL);
    assert(strstr(buf, "No one challenged") != NULL);
    assert(strstr(buf, "Steal") != NULL);
    assert(strstr(buf, "challenged") != NULL);
    assert(strstr(buf, "shuffled back") != NULL);

    printf("PASS\n");
}

static void test_history_blocked_action(void) {
    printf("test_history_blocked_action ... ");

    Game g;
    game_init(&g, 3, 42, 99);

    GameLog log;
    gamelog_init(&log);

    /* Player 0 tries Foreign Aid, Player 1 blocks with Duke */
    log_action(&log, 0, ACT_FOREIGN_AID, -1);
    log_block(&log, 1, DUKE);
    log_pass(&log, 0);
    log_pass(&log, 2);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);

    assert(strstr(buf, "Foreign Aid") != NULL);
    assert(strstr(buf, "blocked") != NULL);
    assert(strstr(buf, "Duke") != NULL);

    printf("PASS\n");
}

static void test_exchange_event(void) {
    printf("test_exchange_event ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    GameLog log;
    gamelog_init(&log);

    log_action(&log, 0, ACT_EXCHANGE, AMBASSADOR);
    log_pass(&log, 1);
    log_exchange(&log, 0);

    /* Player mode: just says "exchanged cards" */
    char buf[BUF_SIZE];
    render_text(&g, 1, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);
    assert(strstr(buf, "exchanged cards") != NULL);

    printf("PASS\n");
}

static void test_game_over_event(void) {
    printf("test_game_over_event ... ");

    Game g;
    game_init(&g, 2, 42, 99);

    GameLog log;
    gamelog_init(&log);
    log_game_over(&log, 0);

    char buf[BUF_SIZE];
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), NULL);
    assert(strstr(buf, "Game over") != NULL);
    assert(strstr(buf, "Player 0 wins") != NULL);

    printf("PASS\n");
}

static void test_full_game_render(void) {
    printf("test_full_game_render ... ");

    /* Play a full game with first-legal-action policy, rendering at each step.
       Validates no crashes and action map consistency throughout. */
    Game g;
    game_init(&g, 4, 777, 888);

    GameLog log;
    gamelog_init(&log);

    char buf[BUF_SIZE];
    int steps = 0;

    while (!is_done(&g) && steps < 500) {
        assert(!is_chance_node(&g));

        int active = get_active_player_ext(&g);
        int phase = get_phase(&g);
        uint32_t mask = get_valid_actions(&g);
        int action = __builtin_ctz(mask);

        /* Render for active player */
        TextActionMap map;
        render_text(&g, active, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);
        assert(map.num_options > 0);
        assert(strlen(buf) > 0);

        /* Log the event based on phase */
        if (phase == PHASE_MAIN_ACTION) {
            log_action(&log, active, action, -1);
        } else if (phase == PHASE_CHALLENGE_ACTION || phase == PHASE_CHALLENGE_BLOCK) {
            if (action == ACT_CHALLENGE) {
                int claimant = get_turn_player(&g);
                if (phase == PHASE_CHALLENGE_BLOCK)
                    claimant = get_blocker(&g);
                log_challenge(&log, active, claimant);
            } else {
                log_pass(&log, active);
            }
        } else if (phase == PHASE_BLOCK) {
            if (action >= ACT_BLOCK_CONTESSA && action <= ACT_BLOCK_DUKE) {
                int role = -1;
                if (action == ACT_BLOCK_DUKE) role = DUKE;
                else if (action == ACT_BLOCK_CAPTAIN) role = CAPTAIN;
                else if (action == ACT_BLOCK_AMBASSADOR) role = AMBASSADOR;
                else if (action == ACT_BLOCK_CONTESSA) role = CONTESSA;
                log_block(&log, active, role);
            } else {
                log_pass(&log, active);
            }
        } else if (phase == PHASE_LOSE_CARD) {
            int slot = action - ACT_DISCARD_SLOT0;
            int card_type = (slot == 0) ? player_card0_type(&g, active)
                                        : player_card1_type(&g, active);
            log_lose_card(&log, active, card_type);
        } else if (phase == PHASE_EXCHANGE_DISCARD) {
            /* Just log pass for simplicity in this test */
            log_pass(&log, active);
        }

        step_with_rng(&g, action);
        steps++;

        /* Check for eliminations */
        for (int p = 0; p < get_num_players(&g); p++) {
            if (!player_is_alive(&g, p)) {
                /* Could log eliminated, but skip for simplicity */
            }
        }
    }

    assert(is_done(&g));

    printf("PASS (%d steps, winner: Player %d)\n", steps, get_winner(&g));
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    test_gamelog_helpers();
    test_main_action_render();
    test_not_active_player();
    test_info_mode_player();
    test_info_mode_perfect();
    test_action_map_roundtrip();
    test_buffer_truncation();
    test_eliminated_player();
    test_forced_coup();
    test_history_rendering();
    test_history_blocked_action();
    test_exchange_event();
    test_game_over_event();
    test_full_game_render();

    printf("\nAll text_render tests passed.\n");
    return 0;
}
