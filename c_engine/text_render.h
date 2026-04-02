#ifndef TEXT_RENDER_H
#define TEXT_RENDER_H

#include <stdint.h>
#include "coup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Information modes --- */
#define INFO_MODE_PLAYER   0
#define INFO_MODE_PERFECT  1

/* --- Event types --- */
#define EVENT_ACTION            0
#define EVENT_CHALLENGE         1
#define EVENT_CHALLENGE_RESOLVE 2
#define EVENT_BLOCK             3
#define EVENT_PASS              4
#define EVENT_LOSE_CARD         5
#define EVENT_EXCHANGE          6
#define EVENT_ELIMINATED        8
#define EVENT_GAME_OVER         9

/* --- Outcome flags --- */
#define EVENT_OUTCOME_SUCCESS   0x01
#define EVENT_OUTCOME_FAIL      0x02
#define EVENT_OUTCOME_BLOCKED   0x04
#define EVENT_OUTCOME_SHUFFLED  0x08

/* --- No-value sentinel --- */
#define NO_VAL 0xFF

/* --- Structs --- */

typedef struct {
    uint8_t type;
    uint8_t actor;
    uint8_t target;          /* NO_VAL if none */
    uint8_t card_revealed;   /* NO_VAL if none */
    uint8_t action;          /* flat action index (for EVENT_ACTION) */
    uint8_t role_claimed;    /* card type claimed, NO_VAL if none */
    uint8_t outcome;         /* EVENT_OUTCOME_* flags */
    uint8_t _pad;
} GameEvent;

#define MAX_GAME_EVENTS 256

typedef struct {
    GameEvent events[MAX_GAME_EVENTS];
    uint16_t len;
} GameLog;

typedef struct {
    int action_map[32];   /* text option (0-indexed) -> flat action index */
    int num_options;
} TextActionMap;

/* --- Log functions --- */

void gamelog_init(GameLog *log);
void gamelog_push(GameLog *log, GameEvent event);

void log_action(GameLog *log, int actor, int action, int role_claimed);
void log_challenge(GameLog *log, int challenger, int target);
void log_challenge_resolve(GameLog *log, int claimant, int card_revealed, int outcome);
void log_block(GameLog *log, int blocker, int role_claimed);
void log_pass(GameLog *log, int player);
void log_lose_card(GameLog *log, int player, int card_revealed);
void log_exchange(GameLog *log, int player);
void log_eliminated(GameLog *log, int player);
void log_game_over(GameLog *log, int winner);

/* --- Renderer --- */

void render_text(const Game *g, int player_id, const GameLog *log,
                 int info_mode, char *buf, int buf_size,
                 TextActionMap *action_map_out);

int parse_text_action(const TextActionMap *map, int chosen_option);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RENDER_H */
