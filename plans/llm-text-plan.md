# Coup LLM Text Renderer — Design & Implementation Spec

> **Status:** Not yet started. Depends on stable `c_engine/coup_core.{c,h}` and `history.h`.

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Event Log System](#3-event-log-system)
4. [Text Renderer](#4-text-renderer)
5. [Information Modes](#5-information-modes)
6. [Action Map](#6-action-map)
7. [API Surface](#7-api-surface)
8. [File Structure](#8-file-structure)
9. [Implementation Tasks](#9-implementation-tasks)
10. [Testing](#10-testing)
11. [Open Questions](#11-open-questions)

---

## 1. Overview

A pure C text renderer that converts game state + event history into human-readable natural language prompts. Produces two things:

1. **A text prompt** describing the game from a specific player's perspective (or god-view).
2. **An action map** from numbered text options back to flat action indices.

This enables LLMs to play Coup via text. The renderer is decoupled from any LLM API — it just produces strings and maps. A future Python play loop will call the renderer, send the prompt to an LLM, parse the response number, and map it back to an action index.

The renderer is implemented in C so it can generate training data at scale: render millions of game states into (prompt, correct_action) pairs for supervised fine-tuning from strong play trajectories.

---

## 2. Architecture

```
Game struct (ground truth)
    │
    ├─► observe() ──► float tensor (for neural nets)
    │
    └─► render_text() ──► { char *prompt, action_map[], num_options }
              ▲                     (for LLMs)
              │
         GameLog (structured event history)
```

**Key principle:** The renderer does NOT store or mutate game state. It is a pure function of `(Game, player_id, GameLog, info_mode)` → text output. This keeps it side-effect-free and usable in any context.

**Separation of concerns:**
- `step()` advances the game — unchanged.
- The caller records events into a `GameLog` after each step (same pattern as the PufferLib binding pushing to `HistoryBuffer`).
- `render_text()` reads the Game struct and GameLog to produce text.

---

## 3. Event Log System

### Why not reuse HistoryBuffer directly

The existing `HistoryEntry` has 4 bytes: `(acting_player, action, phase, result)`. This is compact and good for the observation tensor, but insufficient for rich text rendering:

- **`result` is never populated** — it's always 0 in the current PufferLib binding.
- **No target info stored** — targeted actions (coup, steal, assassinate) encode the target in the action index (e.g., `ACT_COUP_P0 + target`), but the renderer needs to decode this.
- **No card reveal info** — challenge resolutions reveal cards, which is public information all players should see.
- **No coin deltas** — "gained 3 coins" or "paid 7 coins" requires knowing the outcome.

Rather than bloating `HistoryEntry` (which would impact the PufferLib observation path), the text renderer uses its own structured event log.

### GameEvent struct

```c
typedef struct {
    uint8_t type;           // EVENT_* enum (see below)
    uint8_t actor;          // player who performed the event
    uint8_t target;         // target player (0xFF if none)
    uint8_t card_revealed;  // card type shown (for challenges/deaths), 0xFF if none
    uint8_t action;         // original flat action index (for action events)
    uint8_t role_claimed;   // card type claimed (for claimable actions/blocks)
    uint8_t outcome;        // EVENT_OUTCOME_* flags
    uint8_t _pad;           // alignment
} GameEvent;  // 8 bytes
```

### Event Types

```c
#define EVENT_ACTION         0   // Player took a main action (income, tax, steal, etc.)
#define EVENT_CHALLENGE      1   // Player challenged a claim
#define EVENT_CHALLENGE_RESOLVE 2 // Challenge was resolved (card revealed or not)
#define EVENT_BLOCK          3   // Player blocked an action
#define EVENT_PASS           4   // Player passed on challenge/block opportunity
#define EVENT_LOSE_CARD      5   // Player lost an influence (chose which card to reveal)
#define EVENT_EXCHANGE       6   // Player completed an exchange (no details visible)
#define EVENT_COINS_CHANGED  7   // Player's coins changed (income, tax, steal, coup cost)
#define EVENT_ELIMINATED     8   // Player lost last influence
#define EVENT_GAME_OVER      9   // Game ended, winner determined
```

### Outcome Flags

```c
#define EVENT_OUTCOME_SUCCESS     0x01  // Challenge succeeded (claimant caught bluffing)
#define EVENT_OUTCOME_FAIL        0x02  // Challenge failed (claimant had the card)
#define EVENT_OUTCOME_BLOCKED     0x04  // Action was blocked
#define EVENT_OUTCOME_SHUFFLED    0x08  // Card was shuffled back (successful defense)
```

### GameLog struct

```c
#define MAX_GAME_EVENTS 256

typedef struct {
    GameEvent events[MAX_GAME_EVENTS];
    uint16_t len;
} GameLog;
```

256 events is generous — a typical 6-player game has ~50-100 decision points, each generating 1-3 events. The log is not a ring buffer; it stores the full game. At 8 bytes per event, the full log is 2KB — trivial.

### Who populates the log?

The text renderer module provides helper functions that the caller invokes after each `step()` call. These helpers read the game state diff to determine what happened:

```c
// Call after each step_with_rng() / step_deterministic()
// Compares pre-step and post-step state to emit appropriate events
void log_step(GameLog *log, const Game *before, const Game *after, 
              int action, int acting_player, int phase_before);
```

Alternatively, for simpler integration, provide granular push functions:

```c
void log_action(GameLog *log, int actor, int action, int role_claimed);
void log_challenge(GameLog *log, int challenger, int target);
void log_challenge_resolve(GameLog *log, int claimant, int card_revealed, int outcome);
void log_block(GameLog *log, int blocker, int role_claimed);
void log_pass(GameLog *log, int player);
void log_lose_card(GameLog *log, int player, int card_revealed);
void log_exchange(GameLog *log, int player);
void log_coins_changed(GameLog *log, int player, int delta);  
void log_eliminated(GameLog *log, int player);
void log_game_over(GameLog *log, int winner);
```

**Decision: Use the granular push approach.** It's more explicit, doesn't require snapshotting game state before each step, and the caller (future Python play loop or test harness) has full context about what happened. The `log_step` diff approach is fragile — some events (like which card was revealed during a challenge) aren't easily derivable from state diff alone.

For the PufferLib integration path (bulk SFT data gen), a wrapper function can emit the right log calls by inspecting the phase transitions.

---

## 4. Text Renderer

### render_text signature

```c
typedef struct {
    int action_map[32];   // text_option_number (1-indexed) → flat action index
    int num_options;       // number of valid options presented
} TextActionMap;

void render_text(const Game *g, int player_id, const GameLog *log,
                 int info_mode, char *buf, int buf_size, 
                 TextActionMap *action_map_out);
```

- `buf` / `buf_size`: caller-provided output buffer. Renderer writes as much as fits, always null-terminates.
- `action_map_out`: may be NULL if caller only wants the text (e.g., for logging, not for LLM play).
- Returns nothing — writes into `buf`. If buffer is too small, text is truncated (history is trimmed from oldest first).

### Prompt Structure

The rendered text has these sections in order:

```
1. Player identity + hand
2. Table state (all players' public info)
3. Game log (filtered by info mode)
4. Current situation + available actions (if it's this player's turn)
```

#### Example: Main Action Phase (player-restricted view)

```
You are Player 3. You hold: Duke, Captain (both face-down). Coins: 5.

Table:
  Player 0: 2 coins, 1 influence (revealed: Contessa)
  Player 1: 7 coins, 2 influences
  Player 2: ELIMINATED (revealed: Duke, Assassin)
  Player 4: 3 coins, 1 influence (revealed: Ambassador)
  Player 5: 4 coins, 2 influences

History:
  Player 1 claimed Duke for Tax. No one challenged. Gained 3 coins.
  Player 4 tried to Steal from Player 5, claiming Captain.
    Player 5 blocked, claiming Ambassador. No one challenged the block.

It is your turn. Choose an action:
  1. Income (take 1 coin)
  2. Foreign Aid (take 2 coins)
  3. Tax (claim Duke, take 3 coins)
  4. Coup Player 0 (pay 7 coins)
  5. Steal from Player 1 (claim Captain)
  6. Steal from Player 4 (claim Captain)
  7. Steal from Player 5 (claim Captain)
  8. Assassinate Player 0 (claim Assassin, pay 3 coins)
  9. Assassinate Player 1 (claim Assassin, pay 3 coins)
  10. Assassinate Player 4 (claim Assassin, pay 3 coins)
  11. Assassinate Player 5 (claim Assassin, pay 3 coins)
  12. Exchange (claim Ambassador)
```

#### Example: Challenge/Block Phase

```
Player 1 claims Captain to Steal from you.

Choose your response:
  1. Challenge (call their bluff)
  2. Pass
  3. Block with Captain (claim Captain)
  4. Block with Ambassador (claim Ambassador)
```

#### Example: Lose Card Phase

```
You must lose an influence. Choose a card to reveal:
  1. Duke
  2. Captain
```

#### Example: Exchange Discard Phase

```
You drew from the court deck. Choose a card to return:
  Your hand: Duke, Captain, Ambassador, Contessa
  1. Duke
  2. Captain
  3. Ambassador
  4. Contessa
```

---

## 5. Information Modes

```c
#define INFO_MODE_PLAYER   0   // Restricted to what this player can see (default for LLM play)
#define INFO_MODE_PERFECT  1   // God view — all cards, all details visible
```

### INFO_MODE_PLAYER (restricted view)

Matches real-life Coup information rules:

- **Own cards:** Shown with types ("Duke", "Captain").
- **Other players' cards:** Only shown if revealed (dead influence). Living cards show as "X influences" with no type info.
- **Challenge reveals:** When a challenge occurs, the revealed card is shown to all players (e.g., "Player 2 revealed Duke"). If the defense was successful, note that the card was shuffled back: "Player 2 revealed Duke (shuffled back, drew replacement)."
- **Exchange:** "Player X exchanged cards." No details about what was drawn/returned.
- **Lose card:** "Player X revealed Assassin." (The revealed dead card is public.)
- **Coins:** Always visible (public info).
- **Deck:** Not shown (unknown in real Coup — though card-counting from reveals is possible and left to the LLM).

### INFO_MODE_PERFECT (god view)

Everything visible:
- All players' face-down cards shown with types.
- Deck contents shown (count per card type).
- Exchange details shown (what was drawn, what was returned).
- Useful for debugging, analysis, and training data where you want the model to see everything.

---

## 6. Action Map

The action map bridges numbered text options to flat action indices from `coup_core.h`.

```c
// After render_text(), the caller shows the prompt to the LLM.
// LLM responds with e.g. "3".
// Caller converts: flat_action = action_map_out->action_map[3 - 1]
// (text options are 1-indexed for human readability)
```

The map is built dynamically from `get_valid_actions()`:

1. Call `get_valid_actions(g)` to get the 32-bit mask.
2. For each set bit, generate the human-readable text for that action.
3. Number them sequentially starting from 1.
4. Store the mapping: `action_map[i] = flat_action_index`.

Action text is generated by a lookup + context:

| Action Index | Text Template |
|---|---|
| `ACT_INCOME` (0) | "Income (take 1 coin)" |
| `ACT_FOREIGN_AID` (1) | "Foreign Aid (take 2 coins)" |
| `ACT_TAX` (2) | "Tax (claim Duke, take 3 coins)" |
| `ACT_EXCHANGE` (3) | "Exchange (claim Ambassador)" |
| `ACT_COUP_P0 + t` (4-9) | "Coup Player {t} (pay 7 coins)" |
| `ACT_STEAL_P0 + t` (10-15) | "Steal from Player {t} (claim Captain)" |
| `ACT_ASSASSINATE_P0 + t` (16-21) | "Assassinate Player {t} (claim Assassin, pay 3 coins)" |
| `ACT_CHALLENGE` (22) | "Challenge (call their bluff)" |
| `ACT_PASS` (23) | "Pass" |
| `ACT_BLOCK_CONTESSA` (24) | "Block with Contessa" |
| `ACT_BLOCK_CAPTAIN` (25) | "Block with Captain" |
| `ACT_BLOCK_AMBASSADOR` (26) | "Block with Ambassador" |
| `ACT_BLOCK_DUKE` (27) | "Block with Duke" |
| `ACT_DISCARD_SLOT0-3` (28-31) | "Reveal {card_name}" or "{card_name}" (context-dependent) |

For discard slots 28-31, the text depends on the phase:
- **PHASE_LOSE_CARD:** "Reveal {card}" (the card dies).
- **PHASE_EXCHANGE_DISCARD:** "{card}" (return to deck).

The slot-to-card mapping requires reading the player's hand (and exchange draw cards if in exchange phase) from the Game struct.

---

## 7. API Surface

### Public API (text_render.h)

```c
#ifndef TEXT_RENDER_H
#define TEXT_RENDER_H

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
#define EVENT_COINS_CHANGED     7
#define EVENT_ELIMINATED        8
#define EVENT_GAME_OVER         9

/* --- Outcome flags --- */
#define EVENT_OUTCOME_SUCCESS   0x01
#define EVENT_OUTCOME_FAIL      0x02
#define EVENT_OUTCOME_BLOCKED   0x04
#define EVENT_OUTCOME_SHUFFLED  0x08

/* --- Structs --- */

typedef struct {
    uint8_t type;
    uint8_t actor;
    uint8_t target;
    uint8_t card_revealed;
    uint8_t action;
    uint8_t role_claimed;
    uint8_t outcome;
    uint8_t _pad;
} GameEvent;

#define MAX_GAME_EVENTS 256

typedef struct {
    GameEvent events[MAX_GAME_EVENTS];
    uint16_t len;
} GameLog;

typedef struct {
    int action_map[32];
    int num_options;
} TextActionMap;

/* --- Log functions --- */

void gamelog_init(GameLog *log);
void gamelog_push(GameLog *log, GameEvent event);

/* Convenience push helpers */
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

// Renders game state + history into a text prompt.
// Writes into buf (up to buf_size bytes, always null-terminated).
// If action_map_out is non-NULL and it's player_id's turn to act,
// populates the action map for valid actions.
void render_text(const Game *g, int player_id, const GameLog *log,
                 int info_mode, char *buf, int buf_size,
                 TextActionMap *action_map_out);

// Convert a 1-indexed text option to a flat action index.
// Returns -1 if chosen_option is out of range.
int parse_text_action(const TextActionMap *map, int chosen_option);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RENDER_H */
```

### Internal helpers (in text_render.c, not exposed)

- `card_name(int type)` → `"Duke"`, `"Assassin"`, etc.
- `render_player_state(...)` → one line of player info
- `render_history(...)` → the history section, filtered by info mode
- `render_actions(...)` → the numbered action list + builds action_map
- `snprintf`-based string building throughout (safe, no overflow)

---

## 8. File Structure

```
c_engine/
├── coup_core.h          # (existing) — no changes needed
├── coup_core.c          # (existing) — no changes needed
├── history.h            # (existing) — no changes needed
├── text_render.h        # NEW — GameEvent, GameLog, TextActionMap, render_text API
├── text_render.c        # NEW — renderer implementation + log helpers
├── test_text_render.c   # NEW — standalone tests
└── ...
```

The Makefile gets a new target:

```makefile
test-text: c_engine/test_text_render.c c_engine/text_render.c c_engine/coup_core.c
	$(CC) $(CFLAGS) -o test_text_render $^ && ./test_text_render
```

---

## 9. Implementation Tasks

### Task 1: GameEvent + GameLog structs and helpers
**File:** `text_render.h`, `text_render.c`
**Work:**
- Define `GameEvent`, `GameLog`, `TextActionMap` structs.
- Implement `gamelog_init()`, `gamelog_push()`.
- Implement all `log_*()` convenience functions.
- Pure data plumbing, no rendering logic yet.

### Task 2: Card/action name helpers
**File:** `text_render.c`
**Work:**
- `card_name(int type)` → static string.
- `action_description(int action_index, const Game *g, int player_id)` → human-readable action text.
- Handle all 32 action indices, phase-aware for discard slots.

### Task 3: Player state rendering
**File:** `text_render.c`
**Work:**
- Render the "You are Player X" header with hand info.
- Render the table section (all other players' public state).
- Respect `INFO_MODE_PLAYER` vs `INFO_MODE_PERFECT` for card visibility.

### Task 4: History rendering
**File:** `text_render.c`
**Work:**
- Iterate `GameLog` events and convert each to a readable line.
- Group related events (action → challenge → resolve) into coherent paragraphs.
- Filter by info mode:
  - `INFO_MODE_PLAYER`: hide exchange details, show challenge reveals as public info, note shuffle-back.
  - `INFO_MODE_PERFECT`: show everything including hidden cards and exchange details.
- Handle buffer truncation: if history would overflow the buffer, trim oldest events first.

### Task 5: Action map + action list rendering
**File:** `text_render.c`
**Work:**
- Read `get_valid_actions(g)` mask.
- For each valid action, generate text and assign sequential number.
- Populate `TextActionMap`.
- Implement `parse_text_action()`.
- Only render actions if `get_active_player_ext(g) == player_id` (it's their turn).

### Task 6: Top-level render_text assembly
**File:** `text_render.c`
**Work:**
- Wire together tasks 3-5 into `render_text()`.
- Buffer management: track remaining space, write sections in order, handle truncation gracefully.

### Task 7: Tests
**File:** `test_text_render.c`
**Work:**
- Test log helpers (push events, verify contents).
- Test card/action name helpers.
- Test render_text for each phase:
  - Main action (various player counts, coin states).
  - Challenge/block (all block types).
  - Lose card.
  - Exchange discard.
- Test info modes (verify perfect mode shows hidden cards, player mode doesn't).
- Test action map round-trip: render → pick option → parse → verify correct flat action.
- Test buffer truncation (small buffer, verify no overflow + null termination).
- Test edge cases: eliminated players, 2-player game, forced coup (10+ coins).

### Task 8: Makefile integration
**File:** `Makefile`
**Work:**
- Add `test-text` target.
- Add `text_render.o` to any link targets that need it.

### Dependency Order

```
Task 1 ──► Task 2 ──► Task 3 ──┐
                                ├──► Task 6 ──► Task 7
           Task 2 ──► Task 4 ──┤
                                │
           Task 2 ──► Task 5 ──┘

Task 8 (independent, can be done anytime)
```

Tasks 3, 4, 5 can be done in parallel once Task 2 is complete.

---

## 10. Testing

### Unit Tests (test_text_render.c)

Each test sets up a Game struct in a known state, populates a GameLog with events, calls `render_text()`, and checks the output string contains expected substrings.

```c
// Example test sketch:
void test_main_action_prompt(void) {
    Game g;
    game_init(&g, 4, 42, 42);
    // Manually set up a mid-game state...
    
    GameLog log;
    gamelog_init(&log);
    log_action(&log, 1, ACT_TAX, DUKE);
    log_pass(&log, 0); log_pass(&log, 2); log_pass(&log, 3);
    // ...
    
    char buf[8192];
    TextActionMap map;
    render_text(&g, 0, &log, INFO_MODE_PLAYER, buf, sizeof(buf), &map);
    
    assert(strstr(buf, "You are Player 0") != NULL);
    assert(strstr(buf, "Player 1") != NULL);
    assert(strstr(buf, "Income") != NULL);
    assert(map.num_options > 0);
    
    // Round-trip: first option should map to a valid action
    int flat = parse_text_action(&map, 1);
    assert(flat >= 0 && flat < 32);
    assert((get_valid_actions(&g) >> flat) & 1);
}
```

### Fuzz / Property Tests

- For any game state + log, `render_text` should never write past `buf_size`.
- Every option in the action map should correspond to a set bit in `get_valid_actions()`.
- `parse_text_action` of any option 1..num_options should return a valid action.
- `parse_text_action` of 0 or num_options+1 should return -1.

---

## 11. Open Questions

1. **Event grouping in history text.** Should related events (action → challenge → resolve → lose card) be grouped into one paragraph, or listed as separate lines? Grouped is more readable but adds complexity. **Leaning toward:** grouped, since LLMs benefit from coherent narratives.

2. **Player names.** The spec uses "Player 0", "Player 1", etc. Should we support custom names (e.g., "Alice", "Bob") for readability? **Leaning toward:** not now, keep it simple. Player indices are unambiguous.

3. **Coin change events.** Do we need explicit `EVENT_COINS_CHANGED` events, or can the renderer derive coin state from the current Game struct? The current state is always available; historical coin values are not. **Leaning toward:** include them in the log for history narration ("gained 3 coins", "paid 7 coins") but they're optional — the renderer can work without them if the log is sparse.

4. **PufferLib bulk data gen path.** When generating SFT data at scale through PufferLib, the caller needs to maintain a GameLog alongside the HistoryBuffer. This means the PufferLib binding.c would need modification to also push GameEvents. Alternatively, a conversion function `history_to_gamelog()` could translate HistoryEntry sequences into GameEvents — but this loses information (no card reveals, no outcomes). **Decision deferred** to when the SFT pipeline is in scope.
