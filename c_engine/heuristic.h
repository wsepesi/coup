#ifndef COUP_HEURISTIC_H
#define COUP_HEURISTIC_H

/*
 * heuristic.h — Header-only deterministic heuristic player for Coup.
 *
 * Mirrors the "medium bot" strategy from the TypeScript layer:
 *   - Honest play only (never bluffs — only claims cards it holds)
 *   - Never challenges
 *   - Always blocks if able
 *   - Targets weakest for attacks, strongest for steals/coups
 *
 * Card value ranking (high to low): Duke > Captain > Assassin > Ambassador > Contessa
 */

#include "coup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Card value for ranking (higher = more valuable to keep) */
static inline int card_value(int card_type) {
    switch (card_type) {
        case DUKE:       return 5;
        case CAPTAIN:    return 4;
        case ASSASSIN:   return 3;
        case AMBASSADOR: return 2;
        case CONTESSA:   return 1;
        default:         return 0;
    }
}

/* Check if player holds a specific card type (alive) */
static inline int player_has_card(const Game *g, int p, int card_type) {
    if (player_card0_alive(g, p) && player_card0_type(g, p) == card_type) return 1;
    if (player_card1_alive(g, p) && player_card1_type(g, p) == card_type) return 1;
    return 0;
}

/* Count alive cards for a player */
static inline int player_alive_cards(const Game *g, int p) {
    return player_card0_alive(g, p) + player_card1_alive(g, p);
}

/* Find weakest alive opponent (fewest cards, then fewest coins) */
static inline int find_weakest_opponent(const Game *g, int self) {
    int np = get_num_players(g);
    int best = -1;
    int best_cards = 99, best_coins = 99;
    for (int i = 0; i < np; i++) {
        if (i == self || !player_is_alive(g, i)) continue;
        int cards = player_alive_cards(g, i);
        int coins = player_coins(g, i);
        if (cards < best_cards || (cards == best_cards && coins < best_coins)) {
            best = i;
            best_cards = cards;
            best_coins = coins;
        }
    }
    return best;
}

/* Find strongest alive opponent (most coins, then most cards) */
static inline int find_strongest_opponent(const Game *g, int self) {
    int np = get_num_players(g);
    int best = -1;
    int best_coins = -1, best_cards = -1;
    for (int i = 0; i < np; i++) {
        if (i == self || !player_is_alive(g, i)) continue;
        int coins = player_coins(g, i);
        int cards = player_alive_cards(g, i);
        if (coins > best_coins || (coins == best_coins && cards > best_cards)) {
            best = i;
            best_coins = coins;
            best_cards = cards;
        }
    }
    return best;
}

/* Find richest alive opponent (for steal targeting) */
static inline int find_richest_opponent(const Game *g, int self) {
    return find_strongest_opponent(g, self);
}

/* Pick a random valid action using the game's RNG (mutates RNG state) */
static inline int random_valid_action(Game *g) {
    uint32_t mask = get_valid_actions(g);
    int count = __builtin_popcount(mask);
    if (count <= 0) return 0;
    if (count == 1) return __builtin_ctz(mask);
    int idx = (int)xoshiro256_uniform(&g->rng, (uint32_t)count);
    /* Find the idx-th set bit */
    for (int a = 0; a < 32; a++) {
        if (mask & (1u << a)) {
            if (idx == 0) return a;
            idx--;
        }
    }
    return __builtin_ctz(mask); /* fallback */
}

/* Check if a specific action is valid */
static inline int action_valid(uint32_t mask, int action) {
    return (mask >> action) & 1;
}

/*
 * Choose an action using the medium-bot heuristic.
 *
 * The Game pointer is non-const because random_valid_action may be used
 * as a fallback (mutates RNG). In practice the heuristic is deterministic
 * for all reachable game states.
 */
static inline int heuristic_choose_action(Game *g) {
    uint32_t valid = get_valid_actions(g);
    int phase = get_phase(g);
    int active = get_active_player(g);
    int coins = player_coins(g, active);

    switch (phase) {

    case PHASE_MAIN_ACTION: {
        int weakest = find_weakest_opponent(g, active);
        int strongest = find_strongest_opponent(g, active);

        /* Must coup at 10+ coins */
        if (coins >= 10 && strongest >= 0) {
            int act = ACT_COUP_P0 + strongest;
            if (action_valid(valid, act)) return act;
        }

        /* Voluntary coup at 7+ coins → target strongest */
        if (coins >= 7 && strongest >= 0) {
            int act = ACT_COUP_P0 + strongest;
            if (action_valid(valid, act)) return act;
        }

        /* Tax if holding Duke (honest) */
        if (player_has_card(g, active, DUKE) && action_valid(valid, ACT_TAX))
            return ACT_TAX;

        /* Assassinate if holding Assassin + 3 coins → target weakest */
        if (coins >= 3 && player_has_card(g, active, ASSASSIN) && weakest >= 0) {
            int act = ACT_ASSASSINATE_P0 + weakest;
            if (action_valid(valid, act)) return act;
        }

        /* Exchange if holding Ambassador */
        if (player_has_card(g, active, AMBASSADOR) && action_valid(valid, ACT_EXCHANGE))
            return ACT_EXCHANGE;

        /* Steal if holding Captain → target richest */
        if (player_has_card(g, active, CAPTAIN)) {
            int target = find_richest_opponent(g, active);
            if (target >= 0 && player_coins(g, target) > 0) {
                int act = ACT_STEAL_P0 + target;
                if (action_valid(valid, act)) return act;
            }
        }

        /* Income (always succeeds, +1 coin) — preferred over foreign aid
         * because foreign aid is blockable by Duke, leading to wasted turns
         * and artificially long games. */
        if (action_valid(valid, ACT_INCOME))
            return ACT_INCOME;

        /* Foreign aid (gains 2 coins, blockable by Duke — last resort) */
        if (action_valid(valid, ACT_FOREIGN_AID))
            return ACT_FOREIGN_AID;

        /* Fallback: any valid action */
        return random_valid_action(g);
    }

    case PHASE_CHALLENGE_ACTION:
    case PHASE_CHALLENGE_BLOCK:
        /* Never challenge — always pass */
        if (action_valid(valid, ACT_PASS)) return ACT_PASS;
        return random_valid_action(g);

    case PHASE_BLOCK:
        /* Always block if possible, preferring any block action over pass */
        for (int a = ACT_BLOCK_CONTESSA; a <= ACT_BLOCK_DUKE; a++) {
            if (action_valid(valid, a)) return a;
        }
        /* No block available, pass */
        if (action_valid(valid, ACT_PASS)) return ACT_PASS;
        return random_valid_action(g);

    case PHASE_LOSE_CARD: {
        /* Discard least valuable alive card */
        /* Slots 0-1 are hand cards; only alive slots are valid */
        int slot0_alive = player_card0_alive(g, active);
        int slot1_alive = player_card1_alive(g, active);

        if (slot0_alive && slot1_alive) {
            int val0 = card_value(player_card0_type(g, active));
            int val1 = card_value(player_card1_type(g, active));
            /* Discard the less valuable; if tied, discard slot1 */
            if (val0 <= val1) {
                if (action_valid(valid, ACT_DISCARD_SLOT0)) return ACT_DISCARD_SLOT0;
            } else {
                if (action_valid(valid, ACT_DISCARD_SLOT1)) return ACT_DISCARD_SLOT1;
            }
        }
        /* Only one alive or fallback */
        if (action_valid(valid, ACT_DISCARD_SLOT0)) return ACT_DISCARD_SLOT0;
        if (action_valid(valid, ACT_DISCARD_SLOT1)) return ACT_DISCARD_SLOT1;
        return random_valid_action(g);
    }

    case PHASE_EXCHANGE_DISCARD: {
        /* 4 slots: 0=hand0, 1=hand1, 2=exchange0, 3=exchange1
         * Must discard 2, keeping the 2 most valuable.
         * Valid discard actions: DISCARD_SLOT0..SLOT3.
         * First discard recorded, then second discard from remaining. */

        /* Collect values for all valid discard slots */
        int slot_values[4];
        int card_types[4];
        card_types[0] = player_card0_type(g, active);
        card_types[1] = player_card1_type(g, active);
        card_types[2] = get_exchange_card0(g);
        card_types[3] = get_exchange_card1(g);
        for (int i = 0; i < 4; i++)
            slot_values[i] = card_value(card_types[i]);

        /* Find lowest-value valid discard slot */
        int worst_slot = -1;
        int worst_val = 99;
        for (int s = 0; s < 4; s++) {
            if (action_valid(valid, ACT_DISCARD_SLOT0 + s)) {
                if (slot_values[s] < worst_val) {
                    worst_val = slot_values[s];
                    worst_slot = s;
                }
            }
        }
        if (worst_slot >= 0) return ACT_DISCARD_SLOT0 + worst_slot;
        return random_valid_action(g);
    }

    default:
        return random_valid_action(g);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* COUP_HEURISTIC_H */
