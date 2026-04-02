/*
 * print_c_constants.c
 *
 * Compile and run to dump all engine constants from coup_core.h as key=value
 * pairs. Used by test_constants.py to verify C and C++ engines stay in sync.
 */

#include <stdio.h>
#include "coup_core.h"

int main(void) {
    /* Card types */
    printf("DUKE=%d\n", DUKE);
    printf("ASSASSIN=%d\n", ASSASSIN);
    printf("CAPTAIN=%d\n", CAPTAIN);
    printf("AMBASSADOR=%d\n", AMBASSADOR);
    printf("CONTESSA=%d\n", CONTESSA);

    /* Phases */
    printf("PHASE_DEAL=%d\n", PHASE_DEAL);
    printf("PHASE_CHANCE_REDRAW=%d\n", PHASE_CHANCE_REDRAW);
    printf("PHASE_CHANCE_EXCHANGE=%d\n", PHASE_CHANCE_EXCHANGE);
    printf("PHASE_MAIN_ACTION=%d\n", PHASE_MAIN_ACTION);
    printf("PHASE_CHALLENGE_ACTION=%d\n", PHASE_CHALLENGE_ACTION);
    printf("PHASE_BLOCK=%d\n", PHASE_BLOCK);
    printf("PHASE_CHALLENGE_BLOCK=%d\n", PHASE_CHALLENGE_BLOCK);
    printf("PHASE_LOSE_CARD=%d\n", PHASE_LOSE_CARD);
    printf("PHASE_EXCHANGE_DISCARD=%d\n", PHASE_EXCHANGE_DISCARD);
    printf("PHASE_RESOLVE=%d\n", PHASE_RESOLVE);

    /* Actions */
    printf("ACT_INCOME=%d\n", ACT_INCOME);
    printf("ACT_FOREIGN_AID=%d\n", ACT_FOREIGN_AID);
    printf("ACT_TAX=%d\n", ACT_TAX);
    printf("ACT_EXCHANGE=%d\n", ACT_EXCHANGE);
    printf("ACT_COUP_P0=%d\n", ACT_COUP_P0);
    printf("ACT_STEAL_P0=%d\n", ACT_STEAL_P0);
    printf("ACT_ASSASSINATE_P0=%d\n", ACT_ASSASSINATE_P0);
    printf("ACT_CHALLENGE=%d\n", ACT_CHALLENGE);
    printf("ACT_PASS=%d\n", ACT_PASS);
    printf("ACT_BLOCK_CONTESSA=%d\n", ACT_BLOCK_CONTESSA);
    printf("ACT_BLOCK_CAPTAIN=%d\n", ACT_BLOCK_CAPTAIN);
    printf("ACT_BLOCK_AMBASSADOR=%d\n", ACT_BLOCK_AMBASSADOR);
    printf("ACT_BLOCK_DUKE=%d\n", ACT_BLOCK_DUKE);
    printf("ACT_DISCARD_SLOT0=%d\n", ACT_DISCARD_SLOT0);
    printf("ACT_DISCARD_SLOT1=%d\n", ACT_DISCARD_SLOT1);
    printf("ACT_DISCARD_SLOT2=%d\n", ACT_DISCARD_SLOT2);
    printf("ACT_DISCARD_SLOT3=%d\n", ACT_DISCARD_SLOT3);

    /* Other constants */
    printf("MAX_CHANCE_OUTCOMES=%d\n", MAX_CHANCE_OUTCOMES);
    printf("MAX_PLAYERS=%d\n", MAX_PLAYERS);
    printf("OBS_SIZE=%d\n", OBS_SIZE);

    return 0;
}
