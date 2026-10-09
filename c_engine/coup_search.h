/*
 * coup_search.h — opt-in search API over the C engine (no OpenSpiel needed).
 *
 * Building blocks for imperfect-information search directly on the engine:
 *   (a) public-belief-state search (ReBeL / Player of Games): public state +
 *       per-player beliefs over the canonical private-hand space, chance and
 *       private transitions via instantiated Games;
 *   (b) determinized search (IS-MCTS / Ataraxos style): sample hidden worlds
 *       consistent with one player's view (coup_resample*).
 *
 * Nothing here touches the Game layout or the step hot path: it is a separate
 * translation unit (coup_search.c) plus side structs, like CoupObsTracker.
 * Link coup_search.c only if you use it.
 *
 * Pieces:
 *   1. CoupLogEvent / CoupLog   public history log (+ each player's private
 *                               facts), the C twin of the OpenSpiel adapter's
 *                               event list. coup_step_logged() records+steps.
 *   2. CoupPublicState          everything every player sees, + exact 128-bit
 *                               pack and 64-bit key.
 *   3. Private hands            canonical index of a player's alive hidden
 *                               cards (unordered): 15 / 5 / 1 hands for
 *                               2 / 1 / 0 cards. Deck derivation.
 *   4. coup_game_from_public    public state + hidden assignment -> Game.
 *   5. Belief-update helpers    consistency of a hand with public events.
 *   6. coup_resample            sample a full history consistent with one
 *                               player's view (token/constraint sampler).
 *
 * How ReBeL-style search would use this:
 *   - The search node is a CoupPublicState plus, for each player, a belief
 *     vector over coup_hand_count(coup_public_hand_size(pub, p)) canonical
 *     hands (15 with two live cards). The search owns beliefs and policies.
 *   - Legal actions are a function of the public state only
 *     (coup_public_valid_actions; masks never depend on hidden cards, only
     the exchanger's second pick depends on their own first pick),
 *     so one policy row per (public state, hand) suffices.
 *   - Decision transitions: the next public state is the same for every
 *     hand except where the event reveals something (challenge outcome, lost
 *     card): instantiate a Game per hand assignment with
 *     coup_game_from_public_idx (or update beliefs with
 *     coup_hand_challenge_consistent / coup_hand_remove) and step it.
 *     Beliefs update by Bayes with the acting player's policy.
 *   - Chance transitions (redraw / exchange draws) are private to the
 *     drawer; the deck is the complement of all hands
 *     (coup_deck_from_hidden), so the joint belief is not a product of
 *     marginals in general: a full ReBeL implementation either samples joint
 *     hands or keeps a joint/approximate belief. This header gives the exact
 *     per-world machinery; the approximation is the search's choice.
 *   - Slot order is a label (see "Canonical slot order" below): beliefs over
 *     unordered hands ignore the positional facts the real engine leaks
 *     (which slot was lost, which slot was redrawn). coup_resample respects
 *     them exactly.
 */
#ifndef COUP_SEARCH_H
#define COUP_SEARCH_H

#include <stdint.h>
#include "coup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================== */
/* 1. History log                                                        */
/* ===================================================================== */

/* Upper bounds on a full game, n players. Per turn (one main action):
 *   decisions: 1 main action + 3 response windows of at most n-1 decisions
 *              (challenge-action, block, challenge-block) + 3 LOSE_CARD
 *              (lost action challenge, lost block challenge, coup/assassinate)
 *              + 2 EXCHANGE_DISCARD picks  = 3n + 3 (loose, sound)
 *   chance:    1 redraw after a defended action challenge + 2 exchange draws
 *              (a defended block-challenge redraw cannot co-occur with an
 *              exchange)  = 3
 * plus the 2n deal draws. Every turn ends in advance_turn() (turn_count++)
 * or game over, and is_done() fires at turn_count >= MAX_TURNS. */
#define COUP_MAX_DECISIONS_PER_TURN(n) (3 * (n) + 3)
#define COUP_MAX_CHANCE_PER_TURN 3
#define COUP_MAX_GAME_EVENTS(n) \
    (2 * (n) + MAX_TURNS * (COUP_MAX_DECISIONS_PER_TURN(n) + COUP_MAX_CHANCE_PER_TURN))
/* 4812 for 6 players. */
#define COUP_LOG_CAPACITY COUP_MAX_GAME_EVENTS(MAX_PLAYERS)

/* One applied action (decision or chance outcome). Same facts as the
 * OpenSpiel adapter's LoggedEvent (which is now this type). */
typedef struct {
    uint8_t phase;    /* engine phase BEFORE the action */
    uint8_t actor;    /* decision: acting seat; chance: seat receiving the card */
    uint8_t action;   /* decision: absolute action; chance: card type drawn */
    uint8_t info;     /* LOSE_CARD: revealed card type;
                         CHALLENGE: 1 if the claim was true (challenger loses);
                         CHANCE_REDRAW: hand slot being replaced (public);
                         EXCHANGE_DISCARD: type of the discarded card (private) */
    uint8_t claimant; /* CHALLENGE: seat whose claim is challenged */
    uint8_t role;     /* CHALLENGE: the challenged role */
} CoupLogEvent;

/* Fixed-capacity log (~29 KB). Opt-in; the resampler and view comparison
 * take (events, len) so callers may keep their own storage instead. */
typedef struct {
    int len;
    CoupLogEvent ev[COUP_LOG_CAPACITY];
} CoupLog;

static inline int coup_event_is_chance(const CoupLogEvent *e) {
    return e->phase == PHASE_DEAL || e->phase == PHASE_CHANCE_REDRAW ||
           e->phase == PHASE_CHANCE_EXCHANGE;
}

/* Describe `action` (decision, or card type at a chance node) about to be
 * applied to `before`. Mirrors the engine exactly. Returns 0, or -1 if the
 * action is not legal at `before` (e is then unspecified). */
int coup_event_make(const Game *before, int action, CoupLogEvent *e);

void coup_log_reset(CoupLog *log);
/* Append the event for `action` at `before` (call BEFORE applying it).
 * Returns 0, or -1 if illegal or the log is full. */
int coup_log_record(CoupLog *log, const Game *before, int action);

/* Record + apply one action: a decision (step_deterministic) or, at a
 * chance node, a card type (apply_chance). log may be NULL. Returns 0 / -1
 * (state and log untouched on -1). */
int coup_step_logged(Game *g, CoupLog *log, int action);

/* step_with_rng() with logging: applies the decision, then resolves and
 * logs the following chance nodes from g->rng with the engine's own
 * sampler, so the resulting Game is bit-identical to step_with_rng's. */
int coup_step_logged_rng(Game *g, CoupLog *log, int action);

/* Do two logs look the same to `player`? (The C equivalent of comparing
 * InformationStateString(player).) Public facts must match exactly; own
 * draws and own exchange picks must match; others' draws/picks are free. */
int coup_log_same_view(const CoupLogEvent *a, const CoupLogEvent *b, int len,
                       int player);

/* ===================================================================== */
/* 2. Public state                                                       */
/* ===================================================================== */

#define COUP_CARD_NONE 7  /* revealed[] entry for a live or undealt slot */

/* Everything every player can see. Engine fields that only matter in some
 * phases are normalized to 0 elsewhere (stale values the engine leaves
 * behind would otherwise split equal public states). NOT included: hidden
 * card types, exchange draw types, the first exchange discard's slot. */
typedef struct {
    uint8_t num_players;
    uint8_t phase;
    uint8_t turn_player;
    uint8_t active_player;   /* during PHASE_DEAL: seat being dealt to */
    uint8_t pending_action;  /* raw engine field (deal counter in PHASE_DEAL) */
    uint8_t responded_mask;  /* response-window phases only */
    uint8_t blocker;         /* CHALLENGE_BLOCK, or LOSE_CARD after a block challenge */
    uint8_t block_card;
    uint8_t lose_ctx;        /* LOSE_CARD / CHANCE_REDRAW: continuation */
    uint8_t claimant;        /* LOSE_CARD after a lost action challenge */
    uint8_t claimed_card;
    uint8_t redraw_slot;     /* CHANCE_REDRAW: slot being refilled */
    uint8_t exchange_draws;  /* CHANCE_EXCHANGE: 0/1; EXCHANGE_DISCARD: 2 */
    uint8_t exchange_picks;  /* EXCHANGE_DISCARD: 0/1 picks made */
    uint8_t refund_on_challenge;
    uint8_t deck_size;
    uint8_t coins[MAX_PLAYERS];
    uint8_t card_alive[MAX_PLAYERS];          /* bit k: slot k alive */
    uint8_t revealed[MAX_PLAYERS][2];         /* dead dealt slot: type; else NONE */
    uint16_t turn_count;
} CoupPublicState;

void coup_public_from_game(const Game *g, CoupPublicState *pub);
int coup_public_equal(const CoupPublicState *a, const CoupPublicState *b);
/* Exact, injective 128-bit packing (126 bits used) and a 64-bit hash of it. */
void coup_public_pack(const CoupPublicState *pub, uint64_t out[2]);
uint64_t coup_public_key(const CoupPublicState *pub);

/* Number of hidden (alive, dealt, not being redrawn) cards of player p:
 * the size of p's private hand. */
int coup_public_hand_size(const CoupPublicState *pub, int p);
/* Per-role copies not visible to everyone: 3 - face-up dead cards. */
void coup_public_unseen(const CoupPublicState *pub, int unseen[5]);
/* Legal-action mask at this public state, 0 at chance nodes / terminal.
 * Masks depend only on public fields (alive flags, coins, phase, pending
 * action), never on hidden card types. One exception, returned as 0 here:
 * the exchanger's second EXCHANGE_DISCARD pick, whose mask depends on their
 * own (private) first pick; build a Game with the exchanger's
 * CoupExchangeHidden for that one. */
uint32_t coup_public_valid_actions(const CoupPublicState *pub);
int coup_public_is_chance(const CoupPublicState *pub);
int coup_public_is_terminal(const CoupPublicState *pub);

/* ===================================================================== */
/* 3. Private hands                                                      */
/* ===================================================================== */

/* A hand is the unordered multiset of a player's hidden cards. Indices:
 *   size 2: 15 hands, (a,b) a<=b, ordered (0,0),(0,1)..(0,4),(1,1)..(4,4)
 *   size 1: 5 hands, index = card
 *   size 0: 1 hand, index 0
 *
 * Canonical slot order: with two live cards the engine's slot order is a
 * label. Rules are symmetric under swapping a player's two live slots
 * (masks depend only on alive flags, chance only on the deck multiset), but
 * three things refer to physical slots:
 *   - LOSE_CARD (ACT_DISCARD_SLOT0/1) and EXCHANGE_DISCARD picks 0/1 name a
 *     slot: after a swap the same action reveals/discards the other card.
 *     Map by type with coup_hand_slot_of().
 *   - begin_redraw refills the slot holding the proven role (slot 0 if both
 *     do). The redraw slot is public, so it is relabeled by a swap.
 *   - exchange refill keeps cards in [hand0, hand1, draw0, draw1] order.
 * So a canonicalized Game is equivalent to the original up to relabeling
 * slots (multisets, coins, deck, phases, payoffs evolve identically under
 * the type-mapped actions), but slot indices in its future public events can
 * differ. Do not canonicalize a Game and then replay a recorded action list
 * from the real game that names slots (e.g. an opponent's "lose slot 1");
 * map such actions by type, or use the true arrangement. */
#define COUP_HANDS_2 15
#define COUP_HANDS_1 5

int coup_hand_count(int size);                    /* 15, 5, 1; 0 if bad */
int coup_hand_index(int size, const int cards[]); /* any order; -1 if bad */
/* cards_out sorted ascending; returns 0 / -1. */
int coup_hand_cards(int size, int index, int cards_out[2]);
int coup_hand_multiplicity(int size, int index, int card);
/* {.., card} -> hand without one copy of card (size-1), or -1 if absent. */
int coup_hand_remove(int size, int index, int card);
/* hand + card (size+1), or -1 if size >= 2. */
int coup_hand_add(int size, int index, int card);

/* Player p's hidden cards in the Game: returns the size, fills cards in
 * slot order; *index_out (optional) gets the canonical index. */
int coup_game_hand(const Game *g, int p, int cards_out[2], int *index_out);
/* Slot (0/1) of a live card of `type` held by p, preferring slot 0 like
 * the engine; -1 if none. */
int coup_hand_slot_of(const Game *g, int p, int type);
/* Sort every player's two live hidden cards into ascending slot order.
 * Skips a claimant whose slot is being redrawn (one hidden card) and an
 * exchanger whose first pick was a hand slot (the ordered-pick encoding
 * first < second would otherwise change the reachable discard sets).
 * Returns the bitmask of swapped seats.
 * See "Canonical slot order" above for what this does and doesn't keep. */
int coup_canonicalize_hands(Game *g);

/* Hidden part of an exchange in progress (the exchanger's draws/pick). */
typedef struct {
    uint8_t draw[2];        /* drawn types; COUP_CARD_NONE if not drawn yet */
    uint8_t first_discard;  /* EXCHANGE_DISCARD: 0..2 or FIRST_DISCARD_NONE */
} CoupExchangeHidden;

/* Hidden assignment of a Game: per-slot types of each player's hidden cards
 * (COUP_CARD_NONE elsewhere) and the exchange state. */
void coup_hidden_from_game(const Game *g, uint8_t hands[MAX_PLAYERS][2],
                           CoupExchangeHidden *xh);

/* Deck counts implied by a public state + hidden assignment:
 * 3 per role - hidden cards - face-up cards - pending exchange draws.
 * Returns 0, or -1 if any count would be negative (inconsistent). */
int coup_deck_from_hidden(const CoupPublicState *pub,
                          const uint8_t hands[MAX_PLAYERS][2],
                          const CoupExchangeHidden *xh, int deck_out[5]);

/* ===================================================================== */
/* 4. Construct a Game from public info + hidden assignment              */
/* ===================================================================== */

/* hands[p][k]: type of p's slot k if it is hidden (see
 * coup_public_hand_size), ignored otherwise. xh is required when the
 * public state has exchange draws (CHANCE_EXCHANGE with 1 draw,
 * EXCHANGE_DISCARD), ignored (may be NULL) otherwise. Returns 0, or -1 if
 * the assignment is inconsistent (bad types, too many copies of a role,
 * illegal first discard, missing xh).
 *
 * With the true assignment the result behaves exactly like the original
 * (same public state, legal actions, chance outcomes and transitions).
 * Engine fields the public state normalizes away are reset to the values
 * the engine would hold; g->rng is seeded with 0 (reseed it if you use
 * step_with_rng). */
int coup_game_from_public(const CoupPublicState *pub,
                          const uint8_t hands[MAX_PLAYERS][2],
                          const CoupExchangeHidden *xh, Game *out);

/* Same, from canonical hand indices (two-card hands placed in ascending
 * slot order). */
int coup_game_from_public_idx(const CoupPublicState *pub,
                              const int hand_index[MAX_PLAYERS],
                              const CoupExchangeHidden *xh, Game *out);

/* ===================================================================== */
/* 5. Belief-update helpers                                              */
/* ===================================================================== */

/* Is hand (size, index) of the claimant consistent with a challenge on
 * `role` whose outcome was claim_true (1: claimant showed the role)? */
int coup_hand_challenge_consistent(int size, int index, int role,
                                   int claim_true);
/* Lost/revealed card r: the hand becomes coup_hand_remove(size, index, r)
 * (-1 = inconsistent). A defended challenge: the proven card goes back to
 * the deck (coup_hand_remove) and a redraw (coup_hand_add) follows. */

/* ===================================================================== */
/* 6. Resampling a hidden world from one player's view                   */
/* ===================================================================== */

/* Uniform double in [0, 1). */
typedef double (*CoupUniformFn)(void *ctx);
/* CoupUniformFn over a Xoshiro256* ctx. */
double coup_xoshiro_uniform(void *ctx);

/* Sample a full action list (length len, replayable from
 * game_init(g, num_players, 0, 0) through coup_step_logged / apply_chance /
 * step_deterministic) that `player` cannot tell from events[0..len): same
 * public actions, same own cards and draws, every public revelation
 * reproduced. Opponents' hidden draws come from the deck, their unobserved
 * exchange picks uniformly over legal picks. The algorithm formerly in the
 * C++ adapter, plus a fallback repair that keeps very long exchange-heavy
 * histories resamplable (see coup_search.c). Returns the number of attempts used
 * (>= 1), -1 if none succeeded in max_attempts (<= 0: 100000), -2 if the
 * log is malformed or longer than COUP_LOG_CAPACITY. */
int coup_resample(const CoupLogEvent *events, int len, int num_players,
                  int player, CoupUniformFn uniform, void *ctx,
                  uint8_t *actions_out, int max_attempts);

/* coup_resample + replay: writes the resampled Game (and its log, if
 * log_out != NULL). refund: game_set_refund_on_challenge flag. Same
 * return codes; -3 if the replay rejected an action (bug). */
int coup_resample_game(const CoupLogEvent *events, int len, int num_players,
                       int refund, int player, CoupUniformFn uniform,
                       void *ctx, Game *out, CoupLog *log_out);

#ifdef __cplusplus
}
#endif

#endif /* COUP_SEARCH_H */
