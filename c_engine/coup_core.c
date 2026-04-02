#include "coup_core.h"
#include <string.h>

/* ---- Internal helpers ---- */

static int alive_count(const Game *g) {
    int c = 0;
    int np = get_num_players(g);
    for (int i = 0; i < np; i++)
        if (player_is_alive(g, i)) c++;
    return c;
}

static int dead_mask(const Game *g) {
    int mask = 0;
    int np = get_num_players(g);
    for (int i = 0; i < np; i++)
        if (!player_is_alive(g, i)) mask |= (1 << i);
    /* also set bits for non-existent player slots */
    for (int i = np; i < 6; i++)
        mask |= (1 << i);
    return mask;
}

static int next_alive_player(const Game *g, int after) {
    int np = get_num_players(g);
    for (int i = 1; i < np; i++) {
        int p = (after + i) % np;
        if (player_is_alive(g, p)) return p;
    }
    return after;
}

static void advance_turn(Game *g) {
    int tp = get_turn_player(g);
    int next = next_alive_player(g, tp);
    set_turn_player(g, next);
    set_active_player(g, next);
    set_phase(g, PHASE_MAIN_ACTION);
    set_pending_action(g, 0);
    set_responded_mask(g, dead_mask(g));
    set_first_discard(g, FIRST_DISCARD_NONE);
}

/* Which card is claimed by a given action */
static int claimed_card_for_action(int action) {
    if (action == ACT_TAX) return DUKE;
    if (action >= ACT_STEAL_P0 && action <= ACT_STEAL_P0 + 5) return CAPTAIN;
    if (action >= ACT_ASSASSINATE_P0 && action <= ACT_ASSASSINATE_P0 + 5) return ASSASSIN;
    if (action == ACT_EXCHANGE) return AMBASSADOR;
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

static int action_target(int action) {
    if (action >= ACT_COUP_P0 && action <= ACT_COUP_P0 + 5) return action - ACT_COUP_P0;
    if (action >= ACT_STEAL_P0 && action <= ACT_STEAL_P0 + 5) return action - ACT_STEAL_P0;
    if (action >= ACT_ASSASSINATE_P0 && action <= ACT_ASSASSINATE_P0 + 5) return action - ACT_ASSASSINATE_P0;
    return -1;
}


static int player_has_card(const Game *g, int p, int card_type) {
    if (player_card0_alive(g, p) && player_card0_type(g, p) == card_type) return 1;
    if (player_card1_alive(g, p) && player_card1_type(g, p) == card_type) return 1;
    return 0;
}

/* Find next responder starting from current active_player+1, skipping dead/responded/excluded */
static int find_next_responder(const Game *g, int exclude) {
    int np = get_num_players(g);
    int mask = get_responded_mask(g);
    int start = get_active_player(g);
    for (int i = 1; i <= np; i++) {
        int p = (start + i) % np;
        if (p == exclude) continue;
        if (mask & (1 << p)) continue;
        if (!player_is_alive(g, p)) continue;
        return p;
    }
    return -1; /* all responded */
}

static int all_responded(const Game *g, int exclude) {
    int np = get_num_players(g);
    int mask = get_responded_mask(g);
    for (int i = 0; i < np; i++) {
        if (i == exclude) continue;
        if (!player_is_alive(g, i)) continue;
        if (!(mask & (1 << i))) return 0;
    }
    return 1;
}

/* Start cycling for challenge/block from player after turn_player */
static void start_cycling(Game *g, int phase, int exclude) {
    int np = get_num_players(g);
    int rmask = dead_mask(g);
    /* exclude the excluded player */
    rmask |= (1 << exclude);
    set_responded_mask(g, rmask);
    set_phase(g, phase);

    /* find first responder */
    int tp = get_turn_player(g);
    for (int i = 1; i <= np; i++) {
        int p = (tp + i) % np;
        if (p == exclude) continue;
        if (!player_is_alive(g, p)) continue;
        set_active_player(g, p);
        return;
    }
    /* nobody to cycle through — skip to next phase */
    /* This shouldn't happen in normal play but handle it */
    set_active_player(g, get_turn_player(g));
}

static void goto_resolve(Game *g);
static void resolve_action(Game *g);

/* After challenge action: continue based on whether action is blockable */
static void after_challenge_action_all_pass(Game *g) {
    int pa = get_pending_action(g);
    if (pa == ACT_FOREIGN_AID) {
        /* Foreign aid goes to BLOCK - anyone can block with duke */
        int tp = get_turn_player(g);
        start_cycling(g, PHASE_BLOCK, tp);
    } else if ((pa >= ACT_STEAL_P0 && pa <= ACT_STEAL_P0 + 5) ||
               (pa >= ACT_ASSASSINATE_P0 && pa <= ACT_ASSASSINATE_P0 + 5)) {
        /* Blockable by target only - go to BLOCK */
        int target = action_target(pa);
        if (player_is_alive(g, target)) {
            set_phase(g, PHASE_BLOCK);
            set_active_player(g, target);
            /* responded mask: everyone except target has responded */
            int rmask = 0x3F; /* all responded */
            rmask &= ~(1 << target);
            set_responded_mask(g, rmask);
        } else {
            goto_resolve(g);
        }
    } else {
        /* Unblockable (tax, exchange) - go to resolve */
        goto_resolve(g);
    }
}

static void goto_resolve(Game *g) {
    set_phase(g, PHASE_RESOLVE);
    resolve_action(g);
}

/* Check if game is over after someone loses a card */
static int check_game_over(const Game *g) {
    return alive_count(g) <= 1;
}

/* Context tracking for what to do after LOSE_CARD completes.
 * We encode context in a combination of phase transitions:
 * - _pad field bits 0-3: return_phase after lose_card
 *   0 = advance turn (action cancelled / post-coup / post-assassinate-resolve)
 *   1 = continue action (challenger lost, action proceeds; claimant does redraw first)
 *   2 = block continues (block-challenger lost, block stands -> action cancelled, advance)
 *   3 = block failed (blocker lost challenge, action proceeds -> resolve)
 */
#define LC_ADVANCE_TURN  0
#define LC_CLAIMANT_REDRAW 1
#define LC_BLOCK_STANDS  2
#define LC_BLOCK_FAILED  3
#define LC_RESOLVE_ASSASSINATE 4
#define LC_RESOLVE_COUP 5

static int get_lose_card_context(const Game *g) {
    return g->_pad & 0xF;
}
static void set_lose_card_context(Game *g, int ctx) {
    g->_pad = (g->_pad & ~(uint16_t)0xF) | (uint16_t)(ctx & 0xF);
}

/* Store the challenger player for challenge resolution in _pad bits 4-6 */
static void set_challenger(Game *g, int p) {
    g->_pad = (g->_pad & ~(uint16_t)(0x7 << 4)) | (uint16_t)((p & 0x7) << 4);
}

/* Store claimant for challenge in _pad bits 7-9 */
static int get_claimant(const Game *g) {
    return (g->_pad >> 7) & 0x7;
}
static void set_claimant(Game *g, int p) {
    g->_pad = (g->_pad & ~(uint16_t)(0x7 << 7)) | (uint16_t)((p & 0x7) << 7);
}

/* Store the claimed card type for redraw in _pad bits 10-12 */
static int get_claimed_card_stored(const Game *g) {
    return (g->_pad >> 10) & 0x7;
}
static void set_claimed_card_stored(Game *g, int c) {
    g->_pad = (g->_pad & ~(uint16_t)(0x7 << 10)) | (uint16_t)((c & 0x7) << 10);
}

/* After lose_card completes */
static void after_lose_card(Game *g) {
    if (check_game_over(g)) return;
    int ctx = get_lose_card_context(g);
    switch (ctx) {
    case LC_ADVANCE_TURN:
        advance_turn(g);
        break;
    case LC_CLAIMANT_REDRAW: {
        /* Challenger lost. Claimant shuffles revealed card back, draws replacement. */
        int claimant = get_claimant(g);
        int claimed = get_claimed_card_stored(g);
        /* Put the claimed card back in deck */
        deck_add(g, claimed);
        /* Remove it from claimant's hand */
        if (player_card0_alive(g, claimant) && player_card0_type(g, claimant) == claimed) {
            /* We'll replace card0 — mark type as placeholder, CHANCE_REDRAW will set it */
            set_player_card0_type(g, claimant, 0);
            /* Use exchange_card0 to remember which slot to replace: 0 */
            set_exchange_card0(g, 0);
        } else {
            set_player_card1_type(g, claimant, 0);
            set_exchange_card0(g, 1);
        }
        set_phase(g, PHASE_CHANCE_REDRAW);
        set_active_player(g, claimant);
        break;
    }
    case LC_BLOCK_STANDS:
        /* Block challenge: challenger lost, block stands, action cancelled */
        /* But blocker needs redraw first */
        {
            int blocker = get_blocker(g);
            int block_card = get_block_card(g);
            deck_add(g, block_card);
            if (player_card0_alive(g, blocker) && player_card0_type(g, blocker) == block_card) {
                set_player_card0_type(g, blocker, 0);
                set_exchange_card0(g, 0);
            } else {
                set_player_card1_type(g, blocker, 0);
                set_exchange_card0(g, 1);
            }
            set_phase(g, PHASE_CHANCE_REDRAW);
            set_active_player(g, blocker);
            /* After redraw, we need to advance turn (action cancelled by block) */
            /* We'll repurpose context: after redraw in block_stands, advance turn */
            set_lose_card_context(g, LC_ADVANCE_TURN);
        }
        break;
    case LC_BLOCK_FAILED:
        /* Blocker lost challenge on their block, block fails, action proceeds */
        goto_resolve(g);
        break;
    case LC_RESOLVE_ASSASSINATE:
        /* Target lost card from assassination resolve */
        advance_turn(g);
        break;
    case LC_RESOLVE_COUP:
        advance_turn(g);
        break;
    }
}

/* Resolve the pending action */
static void resolve_action(Game *g) {
    int pa = get_pending_action(g);
    int tp = get_turn_player(g);

    if (pa == ACT_INCOME) {
        set_player_coins(g, tp, player_coins(g, tp) + 1);
        advance_turn(g);
    } else if (pa == ACT_FOREIGN_AID) {
        set_player_coins(g, tp, player_coins(g, tp) + 2);
        advance_turn(g);
    } else if (pa == ACT_TAX) {
        int c = player_coins(g, tp) + 3;
        if (c > 12) c = 12;
        set_player_coins(g, tp, c);
        advance_turn(g);
    } else if (pa == ACT_EXCHANGE) {
        /* Draw 2 cards from deck via chance nodes */
        set_phase(g, PHASE_CHANCE_EXCHANGE);
        set_active_player(g, tp);
        set_exchange_card0(g, 7); /* sentinel: not yet drawn */
        set_exchange_card1(g, 7);
    } else if (pa >= ACT_COUP_P0 && pa <= ACT_COUP_P0 + 5) {
        int target = pa - ACT_COUP_P0;
        if (player_is_alive(g, target)) {
            set_phase(g, PHASE_LOSE_CARD);
            set_active_player(g, target);
            set_lose_card_context(g, LC_RESOLVE_COUP);
        } else {
            advance_turn(g);
        }
    } else if (pa >= ACT_STEAL_P0 && pa <= ACT_STEAL_P0 + 5) {
        int target = pa - ACT_STEAL_P0;
        int steal = player_coins(g, target);
        if (steal > 2) steal = 2;
        set_player_coins(g, target, player_coins(g, target) - steal);
        int c = player_coins(g, tp) + steal;
        if (c > 12) c = 12;
        set_player_coins(g, tp, c);
        advance_turn(g);
    } else if (pa >= ACT_ASSASSINATE_P0 && pa <= ACT_ASSASSINATE_P0 + 5) {
        int target = pa - ACT_ASSASSINATE_P0;
        if (player_is_alive(g, target)) {
            set_phase(g, PHASE_LOSE_CARD);
            set_active_player(g, target);
            set_lose_card_context(g, LC_RESOLVE_ASSASSINATE);
        } else {
            advance_turn(g);
        }
    }
}

/* After CHANCE_REDRAW completes (card replaced), continue */
static void after_redraw(Game *g) {
    int ctx = get_lose_card_context(g);
    if (ctx == LC_ADVANCE_TURN) {
        /* came from block_stands path: after redraw, advance turn */
        advance_turn(g);
        return;
    }
    /* Normal path: claimant redraw after successful defense.
     * The action was NOT cancelled, so continue. */
    int pa = get_pending_action(g);
    /* After challenge_action defense: continue to block or resolve */
    if (pa == ACT_FOREIGN_AID) {
        /* FA doesn't go through challenge_action normally, but handle anyway */
        int tp = get_turn_player(g);
        start_cycling(g, PHASE_BLOCK, tp);
    } else if ((pa >= ACT_STEAL_P0 && pa <= ACT_STEAL_P0 + 5) ||
               (pa >= ACT_ASSASSINATE_P0 && pa <= ACT_ASSASSINATE_P0 + 5)) {
        /* blockable: go to BLOCK */
        int target = action_target(pa);
        if (player_is_alive(g, target)) {
            set_phase(g, PHASE_BLOCK);
            set_active_player(g, target);
            int rmask = 0x3F;
            rmask &= ~(1 << target);
            set_responded_mask(g, rmask);
        } else {
            goto_resolve(g);
        }
    } else {
        /* unblockable: resolve */
        goto_resolve(g);
    }
}

/* ---- Deal phase tracking ----
 * During DEAL, we use:
 *   active_player = player being dealt to
 *   pending_action bits = deal counter (0..2*num_players-1)
 */

static void deal_advance(Game *g) {
    int counter = get_pending_action(g);
    int np = get_num_players(g);
    int total_cards = np * 2;

    counter++;
    if (counter >= total_cards) {
        /* Done dealing, start the game */
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

/* ---- Public API ---- */

void game_init(Game *g, int num_players, uint64_t deal_seed, uint64_t proc_seed) {
    memset(g, 0, sizeof(Game));
    set_num_players(g, num_players);

    /* Initialize deck: 3 of each card type = 15 cards */
    for (int i = 0; i < 5; i++)
        deck_set_count(g, i, 3);

    /* Set all players to 2 coins, cards dead initially (deal will bring alive) */
    for (int i = 0; i < num_players; i++)
        set_player_coins(g, i, 2);

    /* Setup deal phase */
    set_phase(g, PHASE_DEAL);
    set_active_player(g, 0);
    set_pending_action(g, 0);
    set_first_discard(g, FIRST_DISCARD_NONE);

    /* Seed the procedural RNG */
    xoshiro256_seed(&g->rng, proc_seed);

    /* If deal_seed provided, deal using a separate RNG */
    if (deal_seed != 0 || proc_seed != 0) {
        /* Deal will happen via chance nodes (PHASE_DEAL) */
        /* The caller (step_with_rng) will use g->rng to resolve them */
        /* But we want deal randomness from deal_seed */
        /* Store deal_seed's rng temporarily — actually, for step_with_rng,
         * we want the deal to use deal_seed. We'll seed g->rng with deal_seed
         * first, deal all cards, then re-seed with proc_seed. */
        Xoshiro256 saved_rng;
        xoshiro256_seed(&saved_rng, proc_seed);
        xoshiro256_seed(&g->rng, deal_seed);

        /* Auto-deal using the RNG */
        while (get_phase(g) == PHASE_DEAL) {
            int total = deck_total(g);
            if (total == 0) break;
            uint32_t r = xoshiro256_uniform(&g->rng, (uint32_t)total);
            int card = -1;
            int cum = 0;
            for (int i = 0; i < 5; i++) {
                cum += deck_count(g, i);
                if ((int)r < cum) { card = i; break; }
            }
            apply_chance(g, card);
        }

        /* Restore procedural RNG */
        g->rng = saved_rng;
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

void apply_chance(Game *g, int outcome) {
    int ph = get_phase(g);
    switch (ph) {
    case PHASE_DEAL: {
        int counter = get_pending_action(g);
        int p = counter / 2;
        int slot = counter % 2;
        if (slot == 0) {
            set_player_card0_type(g, p, outcome);
            set_player_card0_alive(g, p, 1);
        } else {
            set_player_card1_type(g, p, outcome);
            set_player_card1_alive(g, p, 1);
        }
        deck_remove(g, outcome);
        deal_advance(g);
        break;
    }
    case PHASE_CHANCE_REDRAW: {
        /* Replace the card — slot stored in exchange_card0 */
        int claimant = get_active_player(g);
        int slot = get_exchange_card0(g);
        deck_remove(g, outcome);
        if (slot == 0) {
            set_player_card0_type(g, claimant, outcome);
        } else {
            set_player_card1_type(g, claimant, outcome);
        }
        after_redraw(g);
        break;
    }
    case PHASE_CHANCE_EXCHANGE: {
        deck_remove(g, outcome);
        if (get_exchange_card0(g) == 7) {
            /* First card */
            set_exchange_card0(g, outcome);
        } else {
            /* Second card */
            set_exchange_card1(g, outcome);
            /* Now go to EXCHANGE_DISCARD */
            set_phase(g, PHASE_EXCHANGE_DISCARD);
            set_active_player(g, get_turn_player(g));
            set_first_discard(g, FIRST_DISCARD_NONE);
        }
        break;
    }
    }
}

void step_deterministic(Game *g, int action) {
    int ph = get_phase(g);

    switch (ph) {
    case PHASE_MAIN_ACTION: {
        int tp = get_turn_player(g);
        set_pending_action(g, action);

        if (action == ACT_INCOME) {
            goto_resolve(g);
        } else if (action == ACT_FOREIGN_AID) {
            /* Goes directly to BLOCK (anyone can block with Duke) */
            start_cycling(g, PHASE_BLOCK, tp);
        } else if (action == ACT_TAX || action == ACT_EXCHANGE) {
            start_cycling(g, PHASE_CHALLENGE_ACTION, tp);
        } else if (action >= ACT_COUP_P0 && action <= ACT_COUP_P0 + 5) {
            /* Pay 7 coins immediately */
            set_player_coins(g, tp, player_coins(g, tp) - 7);
            goto_resolve(g);
        } else if (action >= ACT_STEAL_P0 && action <= ACT_STEAL_P0 + 5) {
            start_cycling(g, PHASE_CHALLENGE_ACTION, tp);
        } else if (action >= ACT_ASSASSINATE_P0 && action <= ACT_ASSASSINATE_P0 + 5) {
            /* Pay 3 coins immediately */
            set_player_coins(g, tp, player_coins(g, tp) - 3);
            start_cycling(g, PHASE_CHALLENGE_ACTION, tp);
        }
        break;
    }

    case PHASE_CHALLENGE_ACTION: {
        int tp = get_turn_player(g);
        if (action == ACT_PASS) {
            int ap = get_active_player(g);
            int mask = get_responded_mask(g);
            mask |= (1 << ap);
            set_responded_mask(g, mask);
            if (all_responded(g, tp)) {
                after_challenge_action_all_pass(g);
            } else {
                int next = find_next_responder(g, tp);
                if (next >= 0) set_active_player(g, next);
                else after_challenge_action_all_pass(g);
            }
        } else if (action == ACT_CHALLENGE) {
            /* Resolve challenge against turn_player's claim */
            int challenger = get_active_player(g);
            int claimant = tp;
            int claimed = claimed_card_for_action(get_pending_action(g));
            set_challenger(g, challenger);
            set_claimant(g, claimant);
            set_claimed_card_stored(g, claimed);

            if (player_has_card(g, claimant, claimed)) {
                /* Claimant has the card — challenger loses */
                set_phase(g, PHASE_LOSE_CARD);
                set_active_player(g, challenger);
                set_lose_card_context(g, LC_CLAIMANT_REDRAW);
            } else {
                /* Claimant doesn't have it — claimant loses, action cancelled */
                set_phase(g, PHASE_LOSE_CARD);
                set_active_player(g, claimant);
                set_lose_card_context(g, LC_ADVANCE_TURN);
            }
        }
        break;
    }

    case PHASE_BLOCK: {
        int pa = get_pending_action(g);
        if (action == ACT_PASS) {
            int ap = get_active_player(g);
            int mask = get_responded_mask(g);
            mask |= (1 << ap);
            set_responded_mask(g, mask);

            int tp = get_turn_player(g);
            if (pa == ACT_FOREIGN_AID) {
                /* Multiple players can block — cycle */
                if (all_responded(g, tp)) {
                    goto_resolve(g);
                } else {
                    int next = find_next_responder(g, tp);
                    if (next >= 0) set_active_player(g, next);
                    else goto_resolve(g);
                }
            } else {
                /* Only target could block, and they passed */
                goto_resolve(g);
            }
        } else if (action >= ACT_BLOCK_CONTESSA && action <= ACT_BLOCK_DUKE) {
            /* Someone is blocking */
            int blocker = get_active_player(g);
            set_blocker(g, blocker);
            int block_card = claimed_card_for_block(action);
            set_block_card(g, block_card);

            /* Go to CHALLENGE_BLOCK — turn_player and other alive players can challenge */
            int rmask = dead_mask(g);
            rmask |= (1 << blocker); /* blocker can't challenge their own block */
            set_responded_mask(g, rmask);
            set_phase(g, PHASE_CHALLENGE_BLOCK);

            /* Find first responder — start from turn_player direction */
            int tp = get_turn_player(g);
            int found = 0;
            int np = get_num_players(g);
            for (int i = 0; i < np; i++) {
                int p = (tp + i) % np;
                if (p == blocker) continue;
                if (!player_is_alive(g, p)) continue;
                set_active_player(g, p);
                found = 1;
                break;
            }
            if (!found) {
                /* Nobody can challenge — block stands */
                advance_turn(g);
            }
        }
        break;
    }

    case PHASE_CHALLENGE_BLOCK: {
        int blocker = get_blocker(g);
        if (action == ACT_PASS) {
            int ap = get_active_player(g);
            int mask = get_responded_mask(g);
            mask |= (1 << ap);
            set_responded_mask(g, mask);
            if (all_responded(g, blocker)) {
                /* All passed — block stands, action cancelled */
                advance_turn(g);
            } else {
                int next = find_next_responder(g, blocker);
                if (next >= 0) set_active_player(g, next);
                else advance_turn(g);
            }
        } else if (action == ACT_CHALLENGE) {
            int challenger = get_active_player(g);
            int block_card = get_block_card(g);
            set_challenger(g, challenger);
            set_claimant(g, blocker);
            set_claimed_card_stored(g, block_card);

            if (player_has_card(g, blocker, block_card)) {
                /* Blocker has the card — challenger loses, block stands */
                set_phase(g, PHASE_LOSE_CARD);
                set_active_player(g, challenger);
                set_lose_card_context(g, LC_BLOCK_STANDS);
            } else {
                /* Blocker doesn't have it — blocker loses, block fails, action proceeds */
                set_phase(g, PHASE_LOSE_CARD);
                set_active_player(g, blocker);
                set_lose_card_context(g, LC_BLOCK_FAILED);
            }
        }
        break;
    }

    case PHASE_LOSE_CARD: {
        int ap = get_active_player(g);
        if (action == ACT_DISCARD_SLOT0) {
            set_player_card0_alive(g, ap, 0);
        } else if (action == ACT_DISCARD_SLOT1) {
            set_player_card1_alive(g, ap, 0);
        }
        after_lose_card(g);
        break;
    }

    case PHASE_EXCHANGE_DISCARD: {
        int tp = get_turn_player(g);
        int slot = action - ACT_DISCARD_SLOT0; /* 0-3 */
        int fd = get_first_discard(g);

        if (fd == FIRST_DISCARD_NONE) {
            /* First discard */
            set_first_discard(g, slot);
            /* Stay in EXCHANGE_DISCARD for second pick */
        } else {
            /* Second discard — apply both discards */
            int discard1 = fd;
            int discard2 = slot;

            /* Build the 4 card slots:
             * 0 = player card0, 1 = player card1,
             * 2 = exchange_card0, 3 = exchange_card1 */
            int cards[4];
            int alive[4];
            cards[0] = player_card0_type(g, tp);
            alive[0] = player_card0_alive(g, tp);
            cards[1] = player_card1_type(g, tp);
            alive[1] = player_card1_alive(g, tp);
            cards[2] = get_exchange_card0(g);
            alive[2] = 1;
            cards[3] = get_exchange_card1(g);
            alive[3] = 1;

            /* Return discarded cards to deck */
            deck_add(g, cards[discard1]);
            deck_add(g, cards[discard2]);

            /* Mark discarded slots */
            alive[discard1] = 0;
            alive[discard2] = 0;

            /* Assign remaining cards to player hand slots */
            int kept[4];
            int nkept = 0;
            for (int i = 0; i < 4; i++) {
                if (alive[i]) kept[nkept++] = cards[i];
            }

            /* Player's alive cards should be updated */
            /* First, figure out which hand slots are alive */
            int c0_was_alive = player_card0_alive(g, tp);
            int c1_was_alive = player_card1_alive(g, tp);

            int ki = 0;
            if (c0_was_alive) {
                set_player_card0_type(g, tp, kept[ki++]);
            }
            if (c1_was_alive) {
                set_player_card1_type(g, tp, kept[ki++]);
            }

            advance_turn(g);
        }
        break;
    }

    default:
        break;
    }
}

void step_with_rng(Game *g, int action) {
    step_deterministic(g, action);
    while (is_chance_node(g) && !is_done(g)) {
        int total = deck_total(g);
        if (total == 0) break;
        uint32_t r = xoshiro256_uniform(&g->rng, (uint32_t)total);
        int card = -1;
        int cum = 0;
        for (int i = 0; i < 5; i++) {
            cum += deck_count(g, i);
            if ((int)r < cum) { card = i; break; }
        }
        if (card >= 0) apply_chance(g, card);
        else break;
    }
    /* Also auto-resolve RESOLVE phase */
    while (get_phase(g) == PHASE_RESOLVE && !is_done(g)) {
        resolve_action(g);
        /* resolve_action may chain into chance nodes */
        while (is_chance_node(g) && !is_done(g)) {
            int total = deck_total(g);
            if (total == 0) break;
            uint32_t r = xoshiro256_uniform(&g->rng, (uint32_t)total);
            int card = -1;
            int cum = 0;
            for (int i = 0; i < 5; i++) {
                cum += deck_count(g, i);
                if ((int)r < cum) { card = i; break; }
            }
            if (card >= 0) apply_chance(g, card);
            else break;
        }
    }
}

uint32_t get_valid_actions(const Game *g) {
    int ph = get_phase(g);
    uint32_t mask = 0;
    int np = get_num_players(g);

    switch (ph) {
    case PHASE_MAIN_ACTION: {
        int tp = get_turn_player(g);
        int coins = player_coins(g, tp);

        /* Build alive target mask (exclude self, exclude dead) */
        uint32_t target_mask = 0;
        for (int i = 0; i < np; i++) {
            if (i != tp && player_is_alive(g, i))
                target_mask |= (1u << i);
        }

        if (coins >= 10) {
            /* Must coup */
            mask = target_mask << ACT_COUP_P0;
        } else {
            mask |= (1u << ACT_INCOME);
            mask |= (1u << ACT_FOREIGN_AID);
            mask |= (1u << ACT_TAX);
            mask |= (1u << ACT_EXCHANGE);

            /* Coup targets (need 7+ coins) */
            if (coins >= 7)
                mask |= target_mask << ACT_COUP_P0;

            /* Steal targets */
            mask |= target_mask << ACT_STEAL_P0;

            /* Assassinate targets (need 3+ coins) */
            if (coins >= 3)
                mask |= target_mask << ACT_ASSASSINATE_P0;
        }
        break;
    }

    case PHASE_CHALLENGE_ACTION:
    case PHASE_CHALLENGE_BLOCK:
        mask = (1u << ACT_CHALLENGE) | (1u << ACT_PASS);
        break;

    case PHASE_BLOCK: {
        int pa = get_pending_action(g);
        mask = (1u << ACT_PASS);
        if (pa == ACT_FOREIGN_AID) {
            mask |= (1u << ACT_BLOCK_DUKE);
        } else if (pa >= ACT_STEAL_P0 && pa <= ACT_STEAL_P0 + 5) {
            mask |= (1u << ACT_BLOCK_CAPTAIN);
            mask |= (1u << ACT_BLOCK_AMBASSADOR);
        } else if (pa >= ACT_ASSASSINATE_P0 && pa <= ACT_ASSASSINATE_P0 + 5) {
            mask |= (1u << ACT_BLOCK_CONTESSA);
        }
        break;
    }

    case PHASE_LOSE_CARD: {
        int ap = get_active_player(g);
        if (player_card0_alive(g, ap)) mask |= (1u << ACT_DISCARD_SLOT0);
        if (player_card1_alive(g, ap)) mask |= (1u << ACT_DISCARD_SLOT1);
        break;
    }

    case PHASE_EXCHANGE_DISCARD: {
        int tp = get_turn_player(g);
        int fd = get_first_discard(g);
        int c0_alive = player_card0_alive(g, tp);
        int c1_alive = player_card1_alive(g, tp);

        if (fd == FIRST_DISCARD_NONE) {
            /* First discard: can pick any available slot, but must leave at
             * least one higher-indexed slot for the second discard (canonical
             * ordering: second pick index > first pick index). */
            int avail[4];
            avail[0] = c0_alive;
            avail[1] = c1_alive;
            avail[2] = 1; /* drawn card 0 always available */
            avail[3] = 1; /* drawn card 1 always available */
            for (int s = 0; s < 4; s++) {
                if (!avail[s]) continue;
                /* Check that at least one available slot exists above s */
                int has_higher = 0;
                for (int t = s + 1; t < 4; t++) {
                    if (avail[t]) { has_higher = 1; break; }
                }
                if (has_higher)
                    mask |= (1u << (ACT_DISCARD_SLOT0 + s));
            }
        } else {
            /* Second discard: only slots strictly above first pick */
            for (int s = fd + 1; s < 4; s++) {
                if (s == 0 && !c0_alive) continue;
                if (s == 1 && !c1_alive) continue;
                /* slots 2,3 are always available (drawn cards) */
                mask |= (1u << (ACT_DISCARD_SLOT0 + s));
            }
        }
        break;
    }

    default:
        break;
    }

    return mask;
}

int get_active_player_ext(const Game *g) {
    if (is_chance_node(g)) return -1;
    return get_active_player(g);
}

int is_done(const Game *g) {
    return alive_count(g) <= 1;
}

int get_winner(const Game *g) {
    int np = get_num_players(g);
    int winner = -1;
    int count = 0;
    for (int i = 0; i < np; i++) {
        if (player_is_alive(g, i)) {
            winner = i;
            count++;
        }
    }
    return (count == 1) ? winner : -1;
}

int get_num_players_ext(const Game *g) {
    return get_num_players(g);
}

/* ---- Observation ---- */

int get_observation_size(void) {
    return OBS_SIZE;
}

void observe(const Game *g, int player_id, const HistoryEntry *history,
             int history_len, float *out)
{
    memset(out, 0, OBS_SIZE * sizeof(float));
    int off = 0;

    /* Own card0: one-hot type (5) + alive (1) */
    out[off + player_card0_type(g, player_id)] = 1.0f;
    off += 5;
    out[off] = (float)player_card0_alive(g, player_id);
    off += 1;

    /* Own card1: one-hot type (5) + alive (1) */
    out[off + player_card1_type(g, player_id)] = 1.0f;
    off += 5;
    out[off] = (float)player_card1_alive(g, player_id);
    off += 1;
    /* off == 12 */

    /* Other players (5 slots x 12 floats = 60) */
    int np = get_num_players(g);
    for (int i = 0; i < 5; i++) {
        int p = (player_id + 1 + i) % MAX_PLAYERS;
        if (p < np) {
            /* card0: revealed = one-hot of type; hidden = zeros */
            int c0_alive = player_card0_alive(g, p);
            if (!c0_alive) {
                out[off + player_card0_type(g, p)] = 1.0f;
            }
            off += 5;
            out[off] = c0_alive ? 0.0f : 1.0f;  /* revealed flag */
            off += 1;

            /* card1 */
            int c1_alive = player_card1_alive(g, p);
            if (!c1_alive) {
                out[off + player_card1_type(g, p)] = 1.0f;
            }
            off += 5;
            out[off] = c1_alive ? 0.0f : 1.0f;
            off += 1;
        } else {
            off += 12;
        }
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

    /* History (64 entries x 4 floats = 256) */
    int n = history_len;
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) {
        int base = off + i * 4;
        out[base + 0] = (float)history[i].acting_player / 6.0f;
        out[base + 1] = (float)history[i].action / 32.0f;
        out[base + 2] = (float)history[i].phase / 10.0f;
        out[base + 3] = (float)history[i].result / 255.0f;
    }
    /* off += 256; total = 407 */
}
