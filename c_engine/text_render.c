#include "text_render.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static int buf_printf(char **cur, int *rem, const char *fmt, ...) {
    if (*rem <= 1) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*cur, *rem, fmt, ap);
    va_end(ap);
    if (n > 0 && n < *rem) {
        *cur += n;
        *rem -= n;
    } else if (n >= *rem) {
        *cur += *rem - 1;
        *rem = 1;
    }
    return n;
}

static const char *card_name(int type) {
    switch (type) {
        case DUKE:       return "Duke";
        case ASSASSIN:   return "Assassin";
        case CAPTAIN:    return "Captain";
        case AMBASSADOR: return "Ambassador";
        case CONTESSA:   return "Contessa";
        default:         return "?";
    }
}

/* Extract target player from a targeted action, or -1 */
static int action_target(int action) {
    if (action >= ACT_COUP_P0 && action < ACT_COUP_P0 + 6)
        return action - ACT_COUP_P0;
    if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6)
        return action - ACT_STEAL_P0;
    if (action >= ACT_ASSASSINATE_P0 && action < ACT_ASSASSINATE_P0 + 6)
        return action - ACT_ASSASSINATE_P0;
    return -1;
}

/* Which role is claimed by this action, or -1 */
static int action_role(int action) {
    if (action == ACT_TAX) return DUKE;
    if (action == ACT_EXCHANGE) return AMBASSADOR;
    if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6) return CAPTAIN;
    if (action >= ACT_ASSASSINATE_P0 && action < ACT_ASSASSINATE_P0 + 6) return ASSASSIN;
    return -1;
}

/* Is this action type challengeable? */
static int action_is_challengeable(int action) {
    return action_role(action) >= 0;
}

/* Is this action type blockable? */
static int action_is_blockable(int action) {
    if (action == ACT_FOREIGN_AID) return 1;
    if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6) return 1;
    if (action >= ACT_ASSASSINATE_P0 && action < ACT_ASSASSINATE_P0 + 6) return 1;
    return 0;
}

/* Card name for a discard slot during exchange */
static const char *slot_card_name(const Game *g, int player, int slot) {
    switch (slot) {
        case 0: return card_name(player_card0_type(g, player));
        case 1: return card_name(player_card1_type(g, player));
        case 2: return card_name(get_exchange_card0(g));
        case 3: return card_name(get_exchange_card1(g));
        default: return "?";
    }
}

/* ------------------------------------------------------------------ */
/*  GameLog helpers                                                     */
/* ------------------------------------------------------------------ */

void gamelog_init(GameLog *log) {
    memset(log, 0, sizeof(GameLog));
}

void gamelog_push(GameLog *log, GameEvent event) {
    if (log->len < MAX_GAME_EVENTS) {
        log->events[log->len++] = event;
    }
}

void log_action(GameLog *log, int actor, int action, int role_claimed) {
    GameEvent e = {0};
    e.type = EVENT_ACTION;
    e.actor = (uint8_t)actor;
    e.action = (uint8_t)action;
    int t = action_target(action);
    e.target = (t >= 0) ? (uint8_t)t : NO_VAL;
    e.role_claimed = (role_claimed >= 0) ? (uint8_t)role_claimed : NO_VAL;
    e.card_revealed = NO_VAL;
    gamelog_push(log, e);
}

void log_challenge(GameLog *log, int challenger, int target) {
    GameEvent e = {0};
    e.type = EVENT_CHALLENGE;
    e.actor = (uint8_t)challenger;
    e.target = (uint8_t)target;
    e.card_revealed = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_challenge_resolve(GameLog *log, int claimant, int card_revealed, int outcome) {
    GameEvent e = {0};
    e.type = EVENT_CHALLENGE_RESOLVE;
    e.actor = (uint8_t)claimant;
    e.card_revealed = (card_revealed >= 0) ? (uint8_t)card_revealed : NO_VAL;
    e.outcome = (uint8_t)outcome;
    e.target = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_block(GameLog *log, int blocker, int role_claimed) {
    GameEvent e = {0};
    e.type = EVENT_BLOCK;
    e.actor = (uint8_t)blocker;
    e.role_claimed = (uint8_t)role_claimed;
    e.target = NO_VAL;
    e.card_revealed = NO_VAL;
    gamelog_push(log, e);
}

void log_pass(GameLog *log, int player) {
    GameEvent e = {0};
    e.type = EVENT_PASS;
    e.actor = (uint8_t)player;
    e.target = NO_VAL;
    e.card_revealed = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_lose_card(GameLog *log, int player, int card_revealed) {
    GameEvent e = {0};
    e.type = EVENT_LOSE_CARD;
    e.actor = (uint8_t)player;
    e.card_revealed = (uint8_t)card_revealed;
    e.target = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_exchange(GameLog *log, int player) {
    GameEvent e = {0};
    e.type = EVENT_EXCHANGE;
    e.actor = (uint8_t)player;
    e.target = NO_VAL;
    e.card_revealed = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_eliminated(GameLog *log, int player) {
    GameEvent e = {0};
    e.type = EVENT_ELIMINATED;
    e.actor = (uint8_t)player;
    e.target = NO_VAL;
    e.card_revealed = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

void log_game_over(GameLog *log, int winner) {
    GameEvent e = {0};
    e.type = EVENT_GAME_OVER;
    e.actor = (uint8_t)winner;
    e.target = NO_VAL;
    e.card_revealed = NO_VAL;
    e.role_claimed = NO_VAL;
    gamelog_push(log, e);
}

/* ------------------------------------------------------------------ */
/*  Section renderers                                                   */
/* ------------------------------------------------------------------ */

static void render_identity(char **cur, int *rem, const Game *g,
                            int player_id, int info_mode) {
    (void)info_mode;
    int c0_alive = player_card0_alive(g, player_id);
    int c1_alive = player_card1_alive(g, player_id);
    int c0_type = player_card0_type(g, player_id);
    int c1_type = player_card1_type(g, player_id);
    int coins = player_coins(g, player_id);

    buf_printf(cur, rem, "You are Player %d.", player_id);

    if (c0_alive && c1_alive) {
        buf_printf(cur, rem, " You hold: %s, %s (both face-down).",
                   card_name(c0_type), card_name(c1_type));
    } else if (c0_alive && !c1_alive) {
        buf_printf(cur, rem, " You hold: %s (face-down), %s (revealed).",
                   card_name(c0_type), card_name(c1_type));
    } else if (!c0_alive && c1_alive) {
        buf_printf(cur, rem, " You hold: %s (revealed), %s (face-down).",
                   card_name(c0_type), card_name(c1_type));
    } else {
        buf_printf(cur, rem, " You hold: %s, %s (both revealed).",
                   card_name(c0_type), card_name(c1_type));
    }

    buf_printf(cur, rem, " Coins: %d.\n", coins);
}

static void render_table(char **cur, int *rem, const Game *g,
                         int player_id, int info_mode) {
    int np = get_num_players(g);
    buf_printf(cur, rem, "Table:\n");

    for (int i = 0; i < np; i++) {
        if (i == player_id) continue;

        int c0_alive = player_card0_alive(g, i);
        int c1_alive = player_card1_alive(g, i);
        int alive = c0_alive || c1_alive;
        int coins = player_coins(g, i);

        if (!alive) {
            /* Eliminated — both cards revealed */
            buf_printf(cur, rem, "  Player %d: ELIMINATED (revealed: %s, %s)\n",
                       i, card_name(player_card0_type(g, i)),
                       card_name(player_card1_type(g, i)));
        } else {
            int influences = c0_alive + c1_alive;
            buf_printf(cur, rem, "  Player %d: %d coins, %d influence%s",
                       i, coins, influences, influences > 1 ? "s" : "");

            /* Show revealed cards */
            if (!c0_alive) {
                buf_printf(cur, rem, " (revealed: %s)",
                           card_name(player_card0_type(g, i)));
            } else if (!c1_alive) {
                buf_printf(cur, rem, " (revealed: %s)",
                           card_name(player_card1_type(g, i)));
            }

            /* Perfect info: show hidden cards */
            if (info_mode == INFO_MODE_PERFECT) {
                if (c0_alive && c1_alive) {
                    buf_printf(cur, rem, " [hidden: %s, %s]",
                               card_name(player_card0_type(g, i)),
                               card_name(player_card1_type(g, i)));
                } else if (c0_alive) {
                    buf_printf(cur, rem, " [hidden: %s]",
                               card_name(player_card0_type(g, i)));
                } else if (c1_alive) {
                    buf_printf(cur, rem, " [hidden: %s]",
                               card_name(player_card1_type(g, i)));
                }
            }

            buf_printf(cur, rem, "\n");
        }
    }

    /* Perfect info: show deck */
    if (info_mode == INFO_MODE_PERFECT) {
        buf_printf(cur, rem, "  Deck (%d cards):", deck_total(g));
        for (int c = 0; c < 5; c++) {
            int cnt = deck_count(g, c);
            if (cnt > 0)
                buf_printf(cur, rem, " %dx%s", cnt, card_name(c));
        }
        buf_printf(cur, rem, "\n");
    }
}

/* Render a single action event as the opening line of a group */
static void render_action_event(char **cur, int *rem, const GameEvent *e) {
    int action = e->action;
    int actor = e->actor;

    if (action == ACT_INCOME) {
        buf_printf(cur, rem, "  Player %d took Income", actor);
    } else if (action == ACT_FOREIGN_AID) {
        buf_printf(cur, rem, "  Player %d attempted Foreign Aid", actor);
    } else if (action == ACT_TAX) {
        buf_printf(cur, rem, "  Player %d claimed Duke for Tax", actor);
    } else if (action == ACT_EXCHANGE) {
        buf_printf(cur, rem, "  Player %d claimed Ambassador for Exchange", actor);
    } else if (action >= ACT_COUP_P0 && action < ACT_COUP_P0 + 6) {
        buf_printf(cur, rem, "  Player %d couped Player %d",
                   actor, action - ACT_COUP_P0);
    } else if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6) {
        buf_printf(cur, rem, "  Player %d claimed Captain to Steal from Player %d",
                   actor, action - ACT_STEAL_P0);
    } else if (action >= ACT_ASSASSINATE_P0 && action < ACT_ASSASSINATE_P0 + 6) {
        buf_printf(cur, rem, "  Player %d claimed Assassin to Assassinate Player %d",
                   actor, action - ACT_ASSASSINATE_P0);
    } else {
        buf_printf(cur, rem, "  Player %d did action %d", actor, action);
    }
}

static void render_history(char **cur, int *rem, const GameLog *log,
                           int player_id, int info_mode) {
    (void)player_id; /* history is public; kept for symmetry with callers */
    if (log->len == 0) return;

    buf_printf(cur, rem, "History:\n");

    int i = 0;
    while (i < log->len) {
        const GameEvent *e = &log->events[i];

        if (e->type == EVENT_ACTION) {
            /* Start a new action group */
            int action = e->action;
            int challengeable = action_is_challengeable(action);
            int blockable = action_is_blockable(action);
            int was_challenged = 0;
            int was_blocked = 0;
            int action_cancelled = 0;

            render_action_event(cur, rem, e);
            i++;

            /* Consume subsequent events in this group */
            while (i < log->len && log->events[i].type != EVENT_ACTION) {
                const GameEvent *sub = &log->events[i];

                switch (sub->type) {
                case EVENT_CHALLENGE:
                    was_challenged = 1;
                    buf_printf(cur, rem, ".\n    Player %d challenged", sub->actor);
                    break;

                case EVENT_CHALLENGE_RESOLVE:
                    if (sub->outcome & EVENT_OUTCOME_FAIL) {
                        /* Claimant had the card — challenge failed */
                        if (sub->card_revealed != NO_VAL) {
                            buf_printf(cur, rem, ". Player %d revealed %s",
                                       sub->actor, card_name(sub->card_revealed));
                            if (sub->outcome & EVENT_OUTCOME_SHUFFLED)
                                buf_printf(cur, rem, " (shuffled back, drew replacement)");
                        }
                        buf_printf(cur, rem, ". Challenge failed");
                    } else if (sub->outcome & EVENT_OUTCOME_SUCCESS) {
                        /* Claimant didn't have it — challenge succeeded */
                        buf_printf(cur, rem, ". Challenge succeeded");
                        action_cancelled = 1;
                    }
                    break;

                case EVENT_BLOCK:
                    was_blocked = 1;
                    buf_printf(cur, rem, ".\n    Player %d blocked",
                               sub->actor);
                    if (sub->role_claimed != NO_VAL)
                        buf_printf(cur, rem, ", claiming %s",
                                   card_name(sub->role_claimed));
                    break;

                case EVENT_LOSE_CARD:
                    buf_printf(cur, rem, ". Player %d revealed %s",
                               sub->actor, card_name(sub->card_revealed));
                    break;

                case EVENT_EXCHANGE:
                    if (info_mode == INFO_MODE_PLAYER) {
                        buf_printf(cur, rem, ". Player %d exchanged cards", sub->actor);
                    } else {
                        buf_printf(cur, rem, ". Player %d exchanged cards (details visible)",
                                   sub->actor);
                    }
                    break;

                case EVENT_ELIMINATED:
                    buf_printf(cur, rem, ". Player %d was eliminated", sub->actor);
                    break;

                case EVENT_PASS:
                    /* Passes are absorbed into summary below */
                    break;

                default:
                    break;
                }
                i++;
            }

            /* Summary suffixes */
            if (challengeable && !was_challenged && !action_cancelled) {
                buf_printf(cur, rem, ". No one challenged");
            }
            if (blockable && !was_blocked && !action_cancelled) {
                buf_printf(cur, rem, ". No one blocked");
            }
            if (was_blocked && !action_cancelled) {
                buf_printf(cur, rem, ". Action blocked");
            }

            /* Outcome descriptions for resolved actions */
            if (!action_cancelled && !was_blocked) {
                if (action == ACT_INCOME) {
                    buf_printf(cur, rem, ". Gained 1 coin");
                } else if (action == ACT_FOREIGN_AID) {
                    buf_printf(cur, rem, ". Gained 2 coins");
                } else if (action == ACT_TAX) {
                    buf_printf(cur, rem, ". Gained 3 coins");
                } else if (action >= ACT_STEAL_P0 && action < ACT_STEAL_P0 + 6) {
                    buf_printf(cur, rem, ". Stole coins");
                }
            }

            buf_printf(cur, rem, ".\n");

        } else if (e->type == EVENT_GAME_OVER) {
            buf_printf(cur, rem, "  Game over. Player %d wins.\n", e->actor);
            i++;
        } else {
            /* Orphaned event outside a group — render standalone */
            if (e->type == EVENT_LOSE_CARD) {
                buf_printf(cur, rem, "  Player %d revealed %s.\n",
                           e->actor, card_name(e->card_revealed));
            } else if (e->type == EVENT_ELIMINATED) {
                buf_printf(cur, rem, "  Player %d was eliminated.\n", e->actor);
            }
            i++;
        }
    }
}

static void render_actions(char **cur, int *rem, const Game *g,
                           int player_id, TextActionMap *map) {
    int phase = get_phase(g);
    uint32_t mask = get_valid_actions(g);
    int turn = get_turn_player(g);

    /* Phase-specific header */
    if (phase == PHASE_MAIN_ACTION) {
        if (player_coins(g, player_id) >= 10)
            buf_printf(cur, rem, "You have 10+ coins and must Coup. Choose a target:\n");
        else
            buf_printf(cur, rem, "It is your turn. Choose an action:\n");
    } else if (phase == PHASE_CHALLENGE_ACTION || phase == PHASE_CHALLENGE_BLOCK) {
        buf_printf(cur, rem, "Choose your response:\n");
    } else if (phase == PHASE_BLOCK) {
        /* Describe what's being blocked */
        int pending = get_pending_action(g);
        if (pending == ACT_FOREIGN_AID) {
            buf_printf(cur, rem, "Player %d attempted Foreign Aid. Respond:\n", turn);
        } else if (pending >= ACT_STEAL_P0 && pending < ACT_STEAL_P0 + 6) {
            buf_printf(cur, rem, "Player %d claims Captain to Steal from Player %d. Respond:\n",
                       turn, pending - ACT_STEAL_P0);
        } else if (pending >= ACT_ASSASSINATE_P0 && pending < ACT_ASSASSINATE_P0 + 6) {
            buf_printf(cur, rem, "Player %d claims Assassin to Assassinate Player %d. Respond:\n",
                       turn, pending - ACT_ASSASSINATE_P0);
        } else {
            buf_printf(cur, rem, "Respond:\n");
        }
    } else if (phase == PHASE_LOSE_CARD) {
        buf_printf(cur, rem, "You must lose an influence. Choose a card to reveal:\n");
    } else if (phase == PHASE_EXCHANGE_DISCARD) {
        int first_d = get_first_discard(g);
        if (first_d == FIRST_DISCARD_NONE) {
            /* First pick — show available cards */
            buf_printf(cur, rem, "Exchange: choose a card to return to the deck.\n");
            buf_printf(cur, rem, "  Your cards:");
            if (player_card0_alive(g, turn))
                buf_printf(cur, rem, " %s", card_name(player_card0_type(g, turn)));
            if (player_card1_alive(g, turn))
                buf_printf(cur, rem, " %s", card_name(player_card1_type(g, turn)));
            buf_printf(cur, rem, " + drawn: %s %s\n",
                       card_name(get_exchange_card0(g)),
                       card_name(get_exchange_card1(g)));
        } else {
            buf_printf(cur, rem, "Exchange: choose a second card to return:\n");
        }
    }

    /* Enumerate valid actions */
    int option = 0;
    for (int a = 0; a < 32; a++) {
        if (!((mask >> a) & 1)) continue;

        option++;
        if (map) map->action_map[option - 1] = a;

        /* Generate description */
        if (a == ACT_INCOME) {
            buf_printf(cur, rem, "  %d. Income (take 1 coin)\n", option);
        } else if (a == ACT_FOREIGN_AID) {
            buf_printf(cur, rem, "  %d. Foreign Aid (take 2 coins)\n", option);
        } else if (a == ACT_TAX) {
            buf_printf(cur, rem, "  %d. Tax (claim Duke, take 3 coins)\n", option);
        } else if (a == ACT_EXCHANGE) {
            buf_printf(cur, rem, "  %d. Exchange (claim Ambassador)\n", option);
        } else if (a >= ACT_COUP_P0 && a < ACT_COUP_P0 + 6) {
            buf_printf(cur, rem, "  %d. Coup Player %d (pay 7 coins)\n",
                       option, a - ACT_COUP_P0);
        } else if (a >= ACT_STEAL_P0 && a < ACT_STEAL_P0 + 6) {
            buf_printf(cur, rem, "  %d. Steal from Player %d (claim Captain)\n",
                       option, a - ACT_STEAL_P0);
        } else if (a >= ACT_ASSASSINATE_P0 && a < ACT_ASSASSINATE_P0 + 6) {
            buf_printf(cur, rem, "  %d. Assassinate Player %d (claim Assassin, pay 3 coins)\n",
                       option, a - ACT_ASSASSINATE_P0);
        } else if (a == ACT_CHALLENGE) {
            buf_printf(cur, rem, "  %d. Challenge (call their bluff)\n", option);
        } else if (a == ACT_PASS) {
            buf_printf(cur, rem, "  %d. Pass\n", option);
        } else if (a == ACT_BLOCK_CONTESSA) {
            buf_printf(cur, rem, "  %d. Block with Contessa\n", option);
        } else if (a == ACT_BLOCK_CAPTAIN) {
            buf_printf(cur, rem, "  %d. Block with Captain\n", option);
        } else if (a == ACT_BLOCK_AMBASSADOR) {
            buf_printf(cur, rem, "  %d. Block with Ambassador\n", option);
        } else if (a == ACT_BLOCK_DUKE) {
            buf_printf(cur, rem, "  %d. Block with Duke\n", option);
        } else if (a >= ACT_DISCARD_SLOT0 && a <= ACT_DISCARD_SLOT3) {
            int slot = a - ACT_DISCARD_SLOT0;
            if (phase == PHASE_LOSE_CARD) {
                buf_printf(cur, rem, "  %d. Reveal %s\n", option,
                           slot_card_name(g, get_active_player(g), slot));
            } else {
                /* EXCHANGE_DISCARD */
                buf_printf(cur, rem, "  %d. %s\n", option,
                           slot_card_name(g, turn, slot));
            }
        }
    }

    if (map) map->num_options = option;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void render_text(const Game *g, int player_id, const GameLog *log,
                 int info_mode, char *buf, int buf_size,
                 TextActionMap *action_map_out) {
    if (buf_size <= 0) return;
    buf[0] = '\0';

    char *cur = buf;
    int rem = buf_size;

    render_identity(&cur, &rem, g, player_id, info_mode);
    buf_printf(&cur, &rem, "\n");
    render_table(&cur, &rem, g, player_id, info_mode);
    buf_printf(&cur, &rem, "\n");
    render_history(&cur, &rem, log, player_id, info_mode);
    buf_printf(&cur, &rem, "\n");

    if (action_map_out) {
        action_map_out->num_options = 0;
        int active = get_active_player_ext(g);
        if (active == player_id) {
            render_actions(&cur, &rem, g, player_id, action_map_out);
        }
    }

    /* Ensure null termination */
    if (buf_size > 0) buf[buf_size - 1] = '\0';
}

int parse_text_action(const TextActionMap *map, int chosen_option) {
    if (chosen_option < 1 || chosen_option > map->num_options) return -1;
    return map->action_map[chosen_option - 1];
}
