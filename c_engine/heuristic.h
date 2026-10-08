#ifndef COUP_HEURISTIC_H
#define COUP_HEURISTIC_H

/*
 * heuristic.h — Header-only scripted Coup players, usable as an eval ladder.
 *
 * Levels (heuristic_choose_action_level / heuristic_choose_action_rng):
 *
 *   HEURISTIC_RANDOM    uniform over legal actions.
 *   HEURISTIC_HONEST    never bluffs: only claims roles (actions AND blocks)
 *                       it actually holds; never challenges. Coups the
 *                       strongest opponent at 7+, taxes with Duke,
 *                       assassinates the weakest, exchanges / steals from
 *                       the richest on alternate rounds; otherwise income.
 *   HEURISTIC_COUNTING  HONEST, plus challenges every claim that is provably
 *                       false from public information + its own hand (all 3
 *                       copies of the role are revealed or in its hand), and
 *                       challenges an assassination aimed at its last card
 *                       when it holds no Contessa (it loses that card anyway
 *                       if it passes, so challenging is free).
 *
 * All levels only read the acting player's own cards plus public state.
 * Card value ranking (high to low): Duke > Captain > Assassin > Ambassador > Contessa
 */

#include "coup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEURISTIC_RANDOM   0
#define HEURISTIC_HONEST   1
#define HEURISTIC_COUNTING 2
#define HEURISTIC_NUM_LEVELS 3

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

/* Uniform legal action drawn from `rng` (0 if there is none). */
static inline int random_valid_action_rng(const Game *g, Xoshiro256 *rng) {
    uint32_t mask = get_valid_actions(g);
    int count = __builtin_popcount(mask);
    if (count == 0) return 0;
    int idx = count == 1 ? 0 : (int)xoshiro256_uniform(rng, (uint32_t)count);
    while (idx-- > 0) mask &= mask - 1; /* drop the lowest idx set bits */
    return __builtin_ctz(mask);
}

/* Uniform legal action using the game's own RNG. Note this advances g->rng
 * and therefore changes later chance outcomes of step_with_rng(). */
static inline int random_valid_action(Game *g) {
    return random_valid_action_rng(g, &g->rng);
}

/* Check if a specific action is valid */
static inline int action_valid(uint32_t mask, int action) {
    return (mask >> action) & 1;
}

/* Copies of `card` whose location `me` knows to be outside every other
 * player's hand: revealed (dead) cards anywhere plus `me`'s own live cards. */
static inline int heuristic_known_copies(const Game *g, int me, int card) {
    int n = 0;
    for (int p = 0; p < get_num_players(g); p++) {
        int mine = p == me;
        if ((mine || !player_card0_alive(g, p)) && player_card0_type(g, p) == card) n++;
        if ((mine || !player_card1_alive(g, p)) && player_card1_type(g, p) == card) n++;
    }
    return n;
}

/* Can `me` prove the claim of `card` is a bluff? (All 3 copies accounted for.) */
static inline int heuristic_claim_impossible(const Game *g, int me, int card) {
    return card >= 0 && heuristic_known_copies(g, me, card) >= 3;
}

static inline int heuristic_challenge_decision(const Game *g, int level, int me) {
    if (level < HEURISTIC_COUNTING) return 0;
    int phase = get_phase(g);
    if (phase == PHASE_CHALLENGE_BLOCK)
        return heuristic_claim_impossible(g, me, get_block_card(g));

    int pa = get_pending_action(g);
    int claimed = -1;
    if (pa == ACT_TAX) claimed = DUKE;
    else if (pa == ACT_EXCHANGE) claimed = AMBASSADOR;
    else if (pa >= ACT_STEAL_P0 && pa < ACT_STEAL_P0 + 6) claimed = CAPTAIN;
    else if (pa >= ACT_ASSASSINATE_P0 && pa < ACT_ASSASSINATE_P0 + 6) claimed = ASSASSIN;
    if (heuristic_claim_impossible(g, me, claimed)) return 1;

    /* Assassination on my last card and I can't (honestly) block: passing
     * loses the game for me, challenging can only help. */
    return claimed == ASSASSIN && pa - ACT_ASSASSINATE_P0 == me &&
           player_alive_cards(g, me) == 1 && !player_has_card(g, me, CONTESSA);
}

/*
 * Choose an action for the active player at `level`, drawing any randomness
 * from `rng`. Must be called at a decision node of an unfinished game.
 */
static inline int heuristic_choose_action_rng(const Game *g, int level, Xoshiro256 *rng) {
    uint32_t valid = get_valid_actions(g);
    if (level <= HEURISTIC_RANDOM || __builtin_popcount(valid) <= 1)
        return random_valid_action_rng(g, rng);

    int phase = get_phase(g);
    int active = get_active_player(g);
    int coins = player_coins(g, active);

    switch (phase) {

    case PHASE_MAIN_ACTION: {
        int weakest = find_weakest_opponent(g, active);
        int strongest = find_strongest_opponent(g, active);

        /* Coup at 7+ coins (forced at 10+) → target strongest */
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

        /* Exchange and steal can make no progress (exchange keeps drawing
         * junk; a Captain/Ambassador target blocks every steal), which used
         * to stall ~10% of games until MAX_TURNS. Use them only on even
         * rounds and take income on odd ones, so coins always grow. */
        int productive_round = ((g->turn_count / get_num_players(g)) & 1) == 0;

        /* Exchange if holding Ambassador */
        if (productive_round && player_has_card(g, active, AMBASSADOR) &&
            action_valid(valid, ACT_EXCHANGE))
            return ACT_EXCHANGE;

        /* Steal if holding Captain → target richest */
        if (productive_round && player_has_card(g, active, CAPTAIN)) {
            int target = find_richest_opponent(g, active);
            if (target >= 0 && player_coins(g, target) > 0) {
                int act = ACT_STEAL_P0 + target;
                if (action_valid(valid, act)) return act;
            }
        }

        /* Income (always succeeds) — preferred over foreign aid, which any
         * Duke can block. */
        if (action_valid(valid, ACT_INCOME))
            return ACT_INCOME;
        if (action_valid(valid, ACT_FOREIGN_AID))
            return ACT_FOREIGN_AID;
        return random_valid_action_rng(g, rng);
    }

    case PHASE_CHALLENGE_ACTION:
    case PHASE_CHALLENGE_BLOCK:
        return heuristic_challenge_decision(g, level, active) ? ACT_CHALLENGE : ACT_PASS;

    case PHASE_BLOCK: {
        /* Block only with a role actually held (no bluffs). */
        static const int blocks[4][2] = {
            {ACT_BLOCK_DUKE, DUKE},     {ACT_BLOCK_CAPTAIN, CAPTAIN},
            {ACT_BLOCK_AMBASSADOR, AMBASSADOR}, {ACT_BLOCK_CONTESSA, CONTESSA},
        };
        for (int i = 0; i < 4; i++) {
            if (action_valid(valid, blocks[i][0]) && player_has_card(g, active, blocks[i][1]))
                return blocks[i][0];
        }
        return ACT_PASS;
    }

    case PHASE_LOSE_CARD: {
        /* Two cards: discard the less valuable (slot 0 on a tie). One card:
         * only one action is legal and was handled above. */
        int val0 = card_value(player_card0_type(g, active));
        int val1 = card_value(player_card1_type(g, active));
        return val0 <= val1 ? ACT_DISCARD_SLOT0 : ACT_DISCARD_SLOT1;
    }

    case PHASE_EXCHANGE_DISCARD: {
        /* Slots: 0,1 = hand, 2,3 = drawn. Discard the two lowest-value
         * available cards; the engine wants them in increasing slot order. */
        int types[4] = {
            player_card0_type(g, active), player_card1_type(g, active),
            get_exchange_card0(g), get_exchange_card1(g),
        };
        int first = get_first_discard(g);
        if (first == FIRST_DISCARD_NONE) {
            int avail = player_card0_alive(g, active) |
                        (player_card1_alive(g, active) << 1) | 0xC;
            int lo1 = -1, lo2 = -1; /* two lowest values, lo1 <= lo2 */
            for (int s = 0; s < 4; s++) {
                if (!((avail >> s) & 1)) continue;
                int v = card_value(types[s]);
                if (lo1 < 0 || v < card_value(types[lo1])) { lo2 = lo1; lo1 = s; }
                else if (lo2 < 0 || v < card_value(types[lo2])) { lo2 = s; }
            }
            int act = ACT_DISCARD_SLOT0 + (lo1 < lo2 ? lo1 : lo2);
            if (action_valid(valid, act)) return act;
            return random_valid_action_rng(g, rng);
        }
        /* Second pick: lowest value among the slots still allowed. */
        int best = -1;
        for (int s = first + 1; s < 4; s++) {
            if (action_valid(valid, ACT_DISCARD_SLOT0 + s) &&
                (best < 0 || card_value(types[s]) < card_value(types[best])))
                best = s;
        }
        return best >= 0 ? ACT_DISCARD_SLOT0 + best : random_valid_action_rng(g, rng);
    }

    default:
        return random_valid_action_rng(g, rng);
    }
}

/* As above, drawing randomness from g->rng (see random_valid_action). Only
 * HEURISTIC_RANDOM actually consumes randomness in reachable states. */
static inline int heuristic_choose_action_level(Game *g, int level) {
    return heuristic_choose_action_rng(g, level, &g->rng);
}

/* Original entry point: the honest heuristic. */
static inline int heuristic_choose_action(Game *g) {
    return heuristic_choose_action_level(g, HEURISTIC_HONEST);
}

#ifdef __cplusplus
}
#endif

#endif /* COUP_HEURISTIC_H */
