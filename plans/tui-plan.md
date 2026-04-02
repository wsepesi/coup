Coup TUI Client — Design & Implementation Spec

> **Note (2026-04-02):** This plan is deferred — not in current dev scope. Path references to `shared/coup_core.c` should read `c_engine/coup_core.c` per the updated repo structure. See `dev-plan.md` and `plans/plan.md` for current architecture decisions.

## Table of Contents

1. [Overview](#1-overview)
2. [Setup Flow](#2-setup-flow)
3. [Screen Layout](#3-screen-layout)
4. [Navigation & Input](#4-navigation--input)
5. [Action Selection Flow](#5-action-selection-flow)
6. [Animations](#6-animations)
7. [Bot Turn Display](#7-bot-turn-display)
8. [C Integration](#8-c-integration)
9. [File Structure](#9-file-structure)
10. [Implementation Notes](#10-implementation-notes)

---

## 1. Overview

A single-player TUI client for playtesting Coup against bot opponents. Built with OpenTUI (Zig core, TypeScript/Bun bindings). The C sim is called via Bun's FFI (`bun:ffi`) — no WASM, no port. One human player, remaining seats filled with bots of configurable difficulty.

The TUI has three screens: a setup screen (configure game), the main game screen (play), and a results screen (game over). The game screen uses FrameBuffer for the table layout with simple color-transition animations, and Box/Text/Select components for the action panel and history log.

---

## 2. Setup Flow

Launched from CLI:

```bash
bun run coup-tui
# or with flags:
bun run coup-tui --players 4 --seed 12345 --difficulty hard
```

If no flags provided, an interactive setup screen appears:

```
╔══════════════════════════════════════════════════╗
║                                                  ║
║              C O U P                             ║
║              ─────                               ║
║                                                  ║
║   Players:    ◀ 4 ▶                              ║
║   Your seat:  ◀ 0 ▶                              ║
║   Bot level:  ◀ Medium ▶                         ║
║   Seed:       [ random    ]                      ║
║                                                  ║
║              [ Start Game ]                      ║
║                                                  ║
║   ↑↓ navigate  ◀▶ adjust  Enter confirm          ║
╚══════════════════════════════════════════════════╝
```

Navigate between fields with ↑↓ arrow keys. Adjust values with ←→. The seed field is an Input component — type a number or leave blank for random. Enter on "Start Game" launches the game.

Settings:

| Field | Range | Default | Notes |
|-------|-------|---------|-------|
| Players | 2–6 | 6 | Total including you |
| Your seat | 0 to players-1 | 0 | Which position you play |
| Bot level | Easy / Medium / Hard | Medium | Applies to all bots |
| Seed | blank or uint64 | random | Fixed seed for reproducibility |

---

## 3. Screen Layout

The game screen is a single FrameBuffer filling the terminal, divided into three zones: the table (top, ~70% height), the history ticker (middle, 3–4 lines), and the action panel (bottom, ~20% height).

### Full Screen Mockup

```
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║                         P2 bob ☠                                ║
║                         [Dk][As]  0●                            ║
║                                                                  ║
║       P1 Bot(Med)                          P3 charlie           ║
║       ▓▓  [Ct]  3●                        ▓▓  ▓▓   4●          ║
║                                                                  ║
║                  ┌───────────────────────┐                       ║
║                  │                       │                       ║
║                  │  alice claims Duke    │                       ║
║                  │  for Tax (+3 coins)   │                       ║
║                  │                       │                       ║
║                  │  Waiting for you...   │                       ║
║                  │                       │                       ║
║                  └───────────────────────┘                       ║
║                                                                  ║
║       P5 Bot(Hard)                         P4 alice             ║
║    ▶  ▓▓  [Am]  2●                        ▓▓  ▓▓   5●          ║
║                                                                  ║
║                        ★ P0 YOU ★                               ║
║                    ┌─────────┐┌─────────┐                        ║
║                    │  DUKE   ││ CAPTAIN │                        ║
║                    │   ♦♦♦   ││   ⚓⚓⚓   │                        ║
║                    └─────────┘└─────────┘                        ║
║                         5● coins                                 ║
║                                                                  ║
╠══════════════════════════════════════════════════════════════════╣
║ bob claimed Duke for Tax. No challenge. +3 coins.               ║
║ Bot(Hard) took Income. +1 coin.                                  ║
║ alice claims Duke for Tax...                                     ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  Your turn. Choose an action:                                    ║
║                                                                  ║
║    Income         Foreign Aid       Tax                          ║
║  ▸ Exchange       Coup              Steal                        ║
║    Assassinate                                                   ║
║                                                          ESC quit║
╚══════════════════════════════════════════════════════════════════╝
```

### Table Zone — Player Positions

Players are arranged in an oval around a center action display. Positions are computed based on player count, mapped to fixed regions of the FrameBuffer:

```
6-player layout (seat positions):

              [2]
        [1]         [3]
           (center)
        [5]         [4]
              [0]
             (you)
```

```
4-player layout:

        [1]         [2]
           (center)
        [3]
              [0]
             (you)
```

```
2-player layout:

              [1]
           (center)
              [0]
             (you)
```

You (seat 0 by default) are always at the bottom center. Other players wrap clockwise. Each player's display area is roughly 20 chars wide × 3 rows tall.

### Per-Player Display

```
  P1 Bot(Med)              — name, colored by status
  ▓▓  [Ct]  3●             — cards + coins
```

Card rendering:

| State | Render | Color |
|-------|--------|-------|
| Hidden (alive) | `▓▓` | dim white |
| Revealed (dead) | `[Dk]` `[As]` `[Cp]` `[Am]` `[Ct]` | dark gray |
| Being revealed (animation) | `[??]` → `[Dk]` | yellow → red or green |

Abbreviations: `Dk`=Duke, `As`=Assassin, `Cp`=Captain, `Am`=Ambassador, `Ct`=Contessa.

Alive indicator: `●` (alive, default color), `○` (one card lost, yellow), `☠` (eliminated, dark red).

Coins: number followed by `●`, e.g. `5●`. Flashes green/red on change.

### Your Hand (Bottom Center)

Your cards are rendered larger than other players' cards since they're your private info and the most important thing on screen:

```
                ┌─────────┐┌─────────┐
                │  DUKE   ││ CAPTAIN │
                │   ♦♦♦   ││   ⚓⚓⚓   │
                └─────────┘└─────────┘
                     5● coins
```

Card type symbols (decorative, inside the card box):

| Type | Symbol | Color |
|------|--------|-------|
| Duke | `♦♦♦` | magenta |
| Assassin | `†††` | red |
| Captain | `⚓⚓⚓` | blue |
| Ambassador | `✦✦✦` | green |
| Contessa | `♥♥♥` | pink/rose |

When a card dies, it dims to dark gray and the border changes to a dashed style or dims.

### Center Action Display

Shows what's currently happening. Updates each phase:

```
┌───────────────────────┐
│                       │
│  alice claims Duke    │
│  for Tax (+3 coins)   │
│                       │
│  Waiting for you...   │
│                       │
└───────────────────────┘
```

During bot turns:

```
┌───────────────────────┐
│                       │
│  Bot(Med) challenges  │
│  alice's Duke claim!  │
│                       │
│  Revealing...         │
│                       │
└───────────────────────┘
```

During resolution:

```
┌───────────────────────┐
│                       │
│  alice reveals Duke!  │
│  Challenge failed.    │
│  Bot(Med) loses a     │
│  card.                │
│                       │
└───────────────────────┘
```

### Target Selection Cursor on Table

When selecting a target player, a cursor arrow `▶` appears next to each valid target and moves as the player navigates with ↑↓ arrows. The currently highlighted player also gets a bright border or name highlight:

```
       P1 Bot(Med)                          P3 charlie
       ▓▓  [Ct]  3●                     ▶  ▓▓  ▓▓   4●
                                            ^^^^^^^^^^^
                                            highlighted
```

The arrow moves around the table oval following the spatial positions, not just a linear list. ↑ moves to the player above, ↓ below, ←→ to the sides. The mapping is:

```
6-player navigation (from your perspective at bottom):

         [2]              ↑ from [1] or [3] → [2]
   [1]         [3]        ← from [2] → [1], → from [2] → [3]
                           ↓ from [1] → [5], ↓ from [3] → [4]
   [5]         [4]        ← from center → [5], → from center → [4]
         [0]              ↓ from [5] or [4] → not valid (that's you)
```

Dead players are skipped in navigation. If pressing ↑ would land on a dead player, it jumps to the next living player in that direction.

### History Ticker

A 3-line scrollable area between the table and action panel. Shows the most recent events in plain text. Auto-scrolls to latest. Each line is one game event, rendered by the C `render_text` history output.

New entries appear at the bottom and push older ones up. New entries briefly highlight in white before fading to gray over ~500ms.

---

## 4. Navigation & Input

All navigation uses arrow keys + enter. Number keys are a shortcut but not required.

### Global Keys

| Key | Action |
|-----|--------|
| `↑` `↓` `←` `→` | Navigate current selection |
| `Enter` | Confirm selection |
| `Escape` | Back (in target select) / Quit confirm (in action select) |
| `1`–`9` | Shortcut: select nth option directly |
| `q` | Quit (with confirmation) |

### Selection State Machine

```
IDLE (waiting for your turn)
  │
  ▼ (your turn starts)
ACTION_SELECT
  │
  ├── untargeted action (income, foreign aid, tax, exchange)
  │   └── Enter → confirm → IDLE (wait for resolution + next turn)
  │
  ├── targeted action (coup, steal, assassinate)
  │   └── Enter → TARGET_SELECT
  │                │
  │                ├── Enter → confirm → IDLE
  │                └── Escape → back to ACTION_SELECT
  │
  ├── challenge/block phase (binary choices)
  │   └── Enter → confirm → IDLE
  │
  └── lose card / exchange discard
      └── Enter → confirm → IDLE
```

### Focus Indicator

The currently focused item in the action panel has a `▸` prefix and bright foreground. Unfocused items are dim:

```
    Income         Foreign Aid       Tax
  ▸ Exchange       Coup              Steal
    Assassinate
```

Arrow keys move the `▸` cursor. Actions are laid out in a grid (3 columns), so ←→ moves between columns and ↑↓ between rows. The grid wraps: pressing → on the rightmost column wraps to the leftmost column of the next row.

---

## 5. Action Selection Flow

### Phase: MAIN_ACTION

**Step 1 — Choose action type:**

The action panel shows only action categories that are available (masked). Layout is a grid, max 3 columns:

```
║  Your turn. Choose an action:                                    ║
║                                                                  ║
║  ▸ Income         Foreign Aid       Tax                          ║
║    Exchange       Coup              Steal                        ║
║    Assassinate                                                   ║
```

If coins ≥ 10 (must coup), only shows:

```
║  You must Coup (10+ coins).                                     ║
║                                                                  ║
║  ▸ Coup                                                          ║
```

If coins < 3, Assassinate is absent. If coins < 7, Coup is absent. The grid adjusts dynamically — only valid categories appear.

**Step 2 — Choose target (if targeted):**

On selecting Coup/Steal/Assassinate, the action panel transitions:

```
║  Steal → choose target:                            ESC go back   ║
║                                                                  ║
║  ▸ alice (5●, 2 inf)                                            ║
║    charlie (4●, 2 inf)                                          ║
║    Bot Hard (2●, 1 inf)                                         ║
```

This is a vertical list. ↑↓ to navigate. The table cursor `▶` simultaneously moves to highlight the target player on the table. Enter confirms. Escape returns to action type selection.

The parenthetical shows coins and influence count so you can make informed decisions without looking away from the panel.

### Phase: CHALLENGE_ACTION / CHALLENGE_BLOCK

```
║  alice claims Captain to Steal from you.                        ║
║                                                                  ║
║  ▸ Challenge          Pass                                       ║
```

Horizontal layout, ←→ to navigate. Or for blocks:

```
║  alice claims Captain to Steal from you.                        ║
║                                                                  ║
║  ▸ Challenge       Pass                                          ║
║    Block (Captain) Block (Ambassador)                            ║
```

Grid layout, 2×2. Arrow keys navigate naturally.

### Phase: BLOCK

```
║  alice attempts Foreign Aid.                                    ║
║                                                                  ║
║  ▸ Block (Duke)    Pass                                          ║
```

### Phase: LOSE_CARD

```
║  You must lose an influence. Choose a card:                     ║
║                                                                  ║
║  ▸ Duke            Captain                                       ║
```

←→ to pick. The corresponding card in your hand display highlights/pulses to show which one you're about to lose.

### Phase: EXCHANGE_DISCARD

```
║  Ambassador Exchange. Choose a card to discard (1 of 2):        ║
║                                                                  ║
║  ▸ Duke       Captain       Ambassador       Contessa           ║
║    (yours)    (yours)       (drawn)          (drawn)            ║
```

Shows all available cards with a label indicating origin. On second discard step, already-discarded card is absent.

---

## 6. Animations

All animations are simple color/character transitions using `setTimeout` chains that update FrameBuffer cells. No movement, no sprites. Target: 2-4 frames per animation, 100-200ms per frame.

### Challenge Reveal (~400ms total)

When any player's card is revealed during a challenge:

```
Frame 0 (0ms):    ▓▓          — hidden card, normal color
Frame 1 (150ms):  [??]        — yellow foreground, "flipping"
Frame 2 (300ms):  [Dk]        — revealed type appears
Frame 3 (400ms):  [Dk]        — settles: green if defense succeeded,
                                 red if bluff caught
```

After settling (200ms hold), the card either dims to gray (dead/revealed permanently) or returns to `▓▓` (successful defense — card shuffled back, new card drawn).

Implementation:

```typescript
async function animateChallengeReveal(
  fb: FrameBuffer, x: number, y: number,
  cardType: string, success: boolean
) {
  const DIM = RGBA.fromHex("#666666");
  const YELLOW = RGBA.fromHex("#FFFF00");
  const GREEN = RGBA.fromHex("#00FF00");
  const RED = RGBA.fromHex("#FF4444");
  const BG = RGBA.fromHex("#111111");

  // Frame 1: mystery
  fb.drawText("[??]", x, y, YELLOW, BG);
  await sleep(150);

  // Frame 2: reveal
  const abbr = CARD_ABBREV[cardType]; // "Dk", "As", etc.
  fb.drawText(`[${abbr}]`, x, y, success ? GREEN : RED, BG);
  await sleep(250);

  if (success) {
    // Card goes back to hidden (reshuffled + redrew)
    await sleep(200);
    fb.drawText("▓▓  ", x, y, DIM, BG);
  } else {
    // Card stays revealed (permanently dead)
    fb.drawText(`[${abbr}]`, x, y, RGBA.fromHex("#444444"), BG);
  }
}
```

### Card Death (~300ms)

When a player loses influence (after choosing which card):

```
Frame 0 (0ms):    ▓▓          — hidden card
Frame 1 (100ms):  [Dk]        — type revealed, red background flash
Frame 2 (200ms):  [Dk]        — red fades
Frame 3 (300ms):  [Dk]        — dims to dark gray (permanent)
```

The player's alive indicator updates: `●` → `○` (one down) or `○` → `☠` (eliminated).

### Coin Change (~300ms)

```
Frame 0 (0ms):    3●          — current coins
Frame 1 (50ms):   5●          — new value, bright green (gain) or red (loss)
Frame 2 (200ms):  5●          — color fading back
Frame 3 (300ms):  5●          — default color
```

### Your Hand Card Highlight (~pulsing while choosing)

When in LOSE_CARD or EXCHANGE_DISCARD phase, the selected card in your hand area pulses between bright and normal every 400ms:

```
Pulse on:    ┌─────────┐
             │  DUKE   │    ← bright white border + text
             │   ♦♦♦   │
             └─────────┘

Pulse off:   ┌─────────┐
             │  DUKE   │    ← normal color
             │   ♦♦♦   │
             └─────────┘
```

Toggles every 400ms while the cursor is on that card. Stops when enter is pressed.

### History Entry Fade-In (~500ms)

New history entries appear in bright white, then fade to gray:

```
Frame 0 (0ms):    text in #FFFFFF (white)
Frame 1 (250ms):  text in #AAAAAA
Frame 2 (500ms):  text in #666666 (resting gray)
```

### Bot "Thinking" Indicator

When a bot is deciding, a simple spinner appears next to their name:

```
Frame cycle (every 200ms): ⠋ → ⠙ → ⠹ → ⠸ → ⠼ → ⠴ → ⠦ → ⠧ → ⠇ → ⠏
```

Displayed for 300–800ms (randomized slightly to feel organic), then the bot's action resolves.

---

## 7. Bot Turn Display

Bot turns should be visible, not instant. The player needs to see what happened. Each bot action follows this cadence:

```
1. Bot "thinking" spinner (300-600ms)
2. Center display updates: "Bot(Med) takes Tax (claim Duke)"
3. Challenge round (if applicable):
   - For each other bot responding: brief pause (200ms), then "pass" or "challenge"
   - Your challenge opportunity: full action panel appears, game waits
4. Block round (if applicable): same pattern
5. Resolution: animation plays (coin change, card death, etc.)
6. History entry appended
7. Brief pause (200ms) before next bot's turn
```

When a bot challenges and a card is revealed, the challenge reveal animation plays at normal speed. The player watches the same animation they'd see if they were the one being challenged.

When it's your turn to respond during a bot's action (e.g. challenge or block opportunity), the game pauses, the action panel appears, and the center display shows what's happening. The `▸ Waiting for you...` text in the center tells the player it's their decision.

Fast mode (for testing): pass `--fast` flag to skip all animation delays. Bot turns resolve instantly. Useful for rapidly testing bot behavior or specific game states.

---

## 8. C Integration

OpenTUI runs on Bun, which has native FFI support via `bun:ffi`. Compile the C sim as a shared library and call it directly — no WASM, no subprocess, no serialization.

### Compile

```bash
cc -shared -fPIC -O3 -std=c99 \
  -o libcoup.so \
  shared/coup_core.c shared/text_render.c
# macOS: -dynamiclib -o libcoup.dylib
```

### FFI Bindings

```typescript
import { dlopen, FFIType, ptr, toBuffer, toArrayBuffer } from "bun:ffi";

const lib = dlopen("./libcoup.so", {
  game_init: {
    args: [FFIType.ptr, FFIType.u64, FFIType.u64],
    returns: FFIType.void,
  },
  step_deterministic: {
    args: [FFIType.ptr, FFIType.i32],
    returns: FFIType.void,
  },
  step_with_rng: {
    args: [FFIType.ptr, FFIType.i32, FFIType.ptr],
    returns: FFIType.void,
  },
  get_valid_actions: {
    args: [FFIType.ptr],
    returns: FFIType.u32,
  },
  observe: {
    args: [FFIType.ptr, FFIType.i32, FFIType.ptr],
    returns: FFIType.void,
  },
  is_done: {
    args: [FFIType.ptr],
    returns: FFIType.i32,
  },
  get_winner: {
    args: [FFIType.ptr],
    returns: FFIType.i32,
  },
  get_active_player: {
    args: [FFIType.ptr],
    returns: FFIType.i32,
  },
  get_phase: {
    args: [FFIType.ptr],
    returns: FFIType.i32,
  },
  is_chance_node: {
    args: [FFIType.ptr],
    returns: FFIType.i32,
  },
  chance_outcomes: {
    args: [FFIType.ptr, FFIType.ptr],
    returns: FFIType.i32,
  },
  apply_chance: {
    args: [FFIType.ptr, FFIType.i32],
    returns: FFIType.void,
  },
  render_text: {
    args: [FFIType.ptr, FFIType.i32, FFIType.ptr, FFIType.i32, FFIType.ptr],
    returns: FFIType.void,
  },
});

// Allocate game struct (32 bytes)
const gameBuffer = new ArrayBuffer(32);
const gamePtr = ptr(gameBuffer);

// Allocate observation buffer (410 floats)
const obsBuffer = new Float32Array(410);
const obsPtr = ptr(obsBuffer.buffer);

// Allocate RNG struct (32 bytes for xoshiro256)
const rngBuffer = new ArrayBuffer(32);
const rngPtr = ptr(rngBuffer);
```

### Game Wrapper

```typescript
class CoupGame {
  private gamePtr: number;
  private rngPtr: number;
  private obsBuffer: Float32Array;
  private obsPtr: number;

  constructor(dealSeed: bigint, procSeed: bigint) {
    // Allocate and init
    this.gamePtr = allocGame();
    this.rngPtr = allocRng(procSeed);
    this.obsBuffer = new Float32Array(410);
    this.obsPtr = ptr(this.obsBuffer.buffer);

    lib.symbols.game_init(this.gamePtr, dealSeed, procSeed);
    this.resolveChance();
  }

  get validActions(): number {
    return lib.symbols.get_valid_actions(this.gamePtr);
  }

  get activePlayer(): number {
    return lib.symbols.get_active_player(this.gamePtr);
  }

  get phase(): number {
    return lib.symbols.get_phase(this.gamePtr);
  }

  get done(): boolean {
    return lib.symbols.is_done(this.gamePtr) !== 0;
  }

  get winner(): number {
    return lib.symbols.get_winner(this.gamePtr);
  }

  step(action: number) {
    lib.symbols.step_deterministic(this.gamePtr, action);
    this.resolveChance();
  }

  observe(playerId: number): Float32Array {
    lib.symbols.observe(this.gamePtr, playerId, this.obsPtr);
    return this.obsBuffer;
  }

  private resolveChance() {
    while (lib.symbols.is_chance_node(this.gamePtr)) {
      // Sample and apply chance outcome using RNG
      const outcome = this.sampleChance();
      lib.symbols.apply_chance(this.gamePtr, outcome);
    }
  }

  private sampleChance(): number {
    // Read chance outcomes, sample weighted by probability
    // using the local RNG
    const outBuf = new ArrayBuffer(5 * 12); // 5 × {int outcome, double prob}
    const n = lib.symbols.chance_outcomes(this.gamePtr, ptr(outBuf));
    // ... sample from distribution
    return sampledOutcome;
  }
}
```

### Bot Decision

For heuristic bots, the logic lives in TypeScript (same as the Cloudflare version — portable). For neural bots, use ONNX Runtime Node (runs natively in Bun):

```typescript
import * as ort from "onnxruntime-node";

class NeuralBot {
  private session: ort.InferenceSession;

  async init(modelPath: string) {
    this.session = await ort.InferenceSession.create(modelPath);
  }

  async chooseAction(obs: Float32Array, validMask: number): Promise<number> {
    const input = new ort.Tensor("float32", obs, [1, 410]);
    const results = await this.session.run({ observation: input });
    const logits = results.logits.data as Float32Array;

    // Apply mask
    for (let i = 0; i < 32; i++) {
      if (!((validMask >> i) & 1)) logits[i] = -1e8;
    }

    return sampleFromLogits(logits);
  }
}
```

---

## 9. File Structure

```
tui/
├── index.ts                 # Entry point: parse CLI args, launch setup or game
├── setup.ts                 # Setup screen (player count, seed, difficulty)
├── game.ts                  # Main game loop + state machine
├── renderer/
│   ├── table.ts             # FrameBuffer: player positions, cards, coins, cursor
│   ├── center.ts            # FrameBuffer: center action display
│   ├── hand.ts              # FrameBuffer: your hand (larger card display)
│   ├── history.ts           # Text/ScrollBox: history ticker
│   ├── actions.ts           # Action panel: grid navigation, two-step selection
│   └── animations.ts        # setTimeout-based color transitions
├── engine/
│   ├── ffi.ts               # bun:ffi bindings to libcoup.so
│   ├── game-wrapper.ts      # CoupGame class wrapping C calls
│   └── bot.ts               # Heuristic + neural bot decision logic
├── util/
│   ├── constants.ts         # Card abbreviations, colors, symbols
│   ├── layout.ts            # Player position computation for N players
│   └── sleep.ts             # Promisified setTimeout
├── package.json
└── build.sh                 # Compiles libcoup.so + runs bun
```

### build.sh

```bash
#!/bin/bash
set -e

# Compile shared library
cc -shared -fPIC -O3 -std=c99 \
  -o libcoup.so \
  ../shared/coup_core.c ../shared/text_render.c

# On macOS:
# cc -dynamiclib -O3 -std=c99 -o libcoup.dylib ../shared/coup_core.c ../shared/text_render.c

echo "Built libcoup.so"

# Run TUI
bun run index.ts "$@"
```

---

## 10. Implementation Notes

### Terminal Size

Minimum recommended: 80 columns × 24 rows. Comfortable: 100×30+. On `onResize`, recompute player positions and redraw the FrameBuffer. For terminals smaller than minimum, show a warning text instead of the game.

### Color Palette

Define a consistent palette used everywhere:

```typescript
const COLORS = {
  bg:           "#111111",  // main background
  border:       "#444444",  // box borders
  textDefault:  "#CCCCCC",  // normal text
  textDim:      "#666666",  // inactive / dead
  textBright:   "#FFFFFF",  // highlights, active selection
  you:          "#00FFAA",  // your name + indicators
  enemy:        "#CCCCCC",  // other player names
  bot:          "#888888",  // bot names (slightly dimmer)
  dead:         "#AA0000",  // eliminated player
  coinGain:     "#00FF00",  // coin increase flash
  coinLoss:     "#FF4444",  // coin decrease flash
  challengeWin: "#00FF00",  // successful defense
  challengeFail:"#FF4444",  // caught bluffing
  duke:         "#AA00FF",  // magenta
  assassin:     "#FF0000",  // red
  captain:      "#0088FF",  // blue
  ambassador:   "#00CC44",  // green
  contessa:     "#FF66AA",  // pink
  cursor:       "#FFFF00",  // selection arrow ▸ and ▶
};
```

### State Machine

The TUI game loop is an async state machine:

```typescript
type TUIState =
  | { phase: "waiting" }                    // Not your turn, watching bots
  | { phase: "action_select" }              // Choosing action type
  | { phase: "target_select", action: ActionType }  // Choosing target
  | { phase: "challenge_respond" }          // Challenge / pass
  | { phase: "block_respond" }              // Block / pass
  | { phase: "lose_card" }                  // Pick card to lose
  | { phase: "exchange_discard", step: 1 | 2 }  // Ambassador discard
  | { phase: "game_over" }                  // Results screen

async function gameLoop(game: CoupGame, renderer: Renderer) {
  while (!game.done) {
    if (game.activePlayer === humanSeat) {
      // Show action panel, wait for input
      const action = await getHumanAction(game, renderer);
      game.step(action);
      await playResolutionAnimations(renderer);
    } else {
      // Bot turn
      await playBotTurn(game, renderer, game.activePlayer);
    }
  }
  showGameOver(game, renderer);
}
```

### Seed Display

When using a fixed seed, show it in a corner of the screen so the player can note it for reproduction:

```
                                              seed: 12345
```

When using a random seed, still display it — the player might want to replay an interesting game.

### Game Over Screen

```
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║                      G A M E   O V E R                          ║
║                                                                  ║
║                    Winner: charlie (P4)                          ║
║                                                                  ║
║   Results:                                                       ║
║     1st  charlie        survived                                ║
║     2nd  YOU            eliminated turn 18                      ║
║     3rd  Bot(Hard)      eliminated turn 15                      ║
║     4th  alice          eliminated turn 12                      ║
║     5th  Bot(Med)       eliminated turn 8                       ║
║     6th  bob            eliminated turn 6                       ║
║                                                                  ║
║   Seed: 12345           Duration: 24 turns                      ║
║                                                                  ║
║   [R] Replay same seed    [N] New game    [Q] Quit              ║
╚══════════════════════════════════════════════════════════════════╝
```

`R` restarts with the same seed (play the exact same deal again, try a different strategy). `N` returns to setup screen. `Q` exits.

---

## 11. Development Plan

> **Added 2026-04-02** after design review. Supersedes the deferred note at the top when this work is in scope.

### Decisions from Review

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Engine | C engine via `bun:ffi`, single authoritative game state | Simpler than dual-engine lockstep. OpenSpiel/PufferLib trained agents export to ONNX at inference time → stateless obs→action. |
| Coordination | Separate `packages/game-client/` package | Reused by web, evals, LLM harness. FFI wrapper + agent interface + game loop live here. |
| TUI Framework | OpenTUI (`@opentui/core` on Bun) | FrameBuffer API maps 1:1 to plan. 9.7K stars, daily commits, Bun-native. Validated: has FrameBuffer, RGBA, keyboard, Timeline, Box/Text/Select/ScrollBox. |
| History text | TypeScript-side rendering | No C `render_text()` dependency. TUI interprets state diffs → human-readable strings. |
| Neural bots | Stub ONNX interface, heuristic bots only | No trained models at dev time. Interface ready for plug-in. |
| Bot names | `bot-1`, `bot-2`, … `bot-k` | Functional, unambiguous. |
| Quit key | `q` = quit (with confirm), `ESC` = back only | Avoids ESC ambiguity in nested menus. |
| Runtime | Bun everywhere (no npm) | User preference. `bun install`, `bun run`, `bun test`. |
| Testing | Integration tests only | No visual snapshot tests. Manual QA for rendering. |

### Revised FFI Surface

The C engine exposes `step()` with internal RNG (not the OpenSpiel chance-node API). Corrected FFI bindings:

```typescript
// packages/game-client/src/ffi.ts
import { dlopen, FFIType, ptr } from "bun:ffi";

const lib = dlopen("./libcoup.dylib", {
  game_init:          { args: [FFIType.ptr, FFIType.u64],          returns: FFIType.void },
  game_step:          { args: [FFIType.ptr, FFIType.i32],          returns: FFIType.void },
  game_get_valid_mask:{ args: [FFIType.ptr],                       returns: FFIType.u32  },
  game_observe:       { args: [FFIType.ptr, FFIType.i32, FFIType.ptr], returns: FFIType.void },
  game_is_done:       { args: [FFIType.ptr],                       returns: FFIType.i32  },
  game_get_winner:    { args: [FFIType.ptr],                       returns: FFIType.i32  },
  game_active_player: { args: [FFIType.ptr],                       returns: FFIType.i32  },
  game_get_phase:     { args: [FFIType.ptr],                       returns: FFIType.i32  },
  game_get_coins:     { args: [FFIType.ptr, FFIType.i32],          returns: FFIType.i32  },
  game_get_influence: { args: [FFIType.ptr, FFIType.i32],          returns: FFIType.i32  },
  game_get_card:      { args: [FFIType.ptr, FFIType.i32, FFIType.i32], returns: FFIType.i32 },
  game_is_card_alive: { args: [FFIType.ptr, FFIType.i32, FFIType.i32], returns: FFIType.i32 },
});
```

> The exact function names will match whatever `c_engine/coup_core.c` exports. The above is the expected shape — adjust at integration time.

### Heuristic Bot Tiers

**Easy** — Random legal action. Uniform sample from valid action mask. Never challenges, never blocks. Provides a punching bag for new players.

**Medium** — Rule-based strategy:
- Must coup at 10+ coins (forced), prefers coup at 7+ coins targeting highest-influence player
- Challenges claims that conflict with own hand (e.g., opponent claims Duke, bot holds 2 Dukes)
- Blocks when holding the correct card (Captain blocks steal, Contessa blocks assassinate)
- Never bluffs — only claims roles it actually holds
- Targets weakest opponent (fewest coins, fewest influence) for steal/assassinate
- Takes Tax if holding Duke, Exchange if holding Ambassador, else Income

**Hard** — Probabilistic with tracking:
- Tracks revealed cards and observed claims to build a belief model of who holds what
- Bluffs strategically: claims Duke for Tax when no Dukes revealed and few challengers remain
- Challenges suspicious claims: opponent claims a role when 2+ of that role are known-revealed
- Counter-bluffs: blocks with a claimed Contessa even without one, if assassination would be fatal
- Targets strongest opponent (most coins) for coup/steal, weakest for assassinate
- Adapts aggression based on player count (more conservative with more opponents)

All three tiers implement the same `Agent` interface: `chooseAction(observation: Float32Array, validMask: number): Promise<number>`.

### Package Structure

```
packages/
  game-client/
    src/
      ffi.ts              # Raw dlopen + FFI function bindings
      game.ts             # CoupGame class (high-level wrapper)
      agent.ts            # Agent interface + AgentRegistry
      agents/
        heuristic-easy.ts
        heuristic-medium.ts
        heuristic-hard.ts
        onnx-stub.ts      # Placeholder: loads ONNX model, runs inference
      history.ts          # Pure fn: game event → human-readable string
      types.ts            # CardType, ActionType, Phase, enums, constants
    package.json
    tsconfig.json

tui/
    src/
      index.ts            # CLI entry point: parse args, launch setup or game
      setup.ts            # Setup screen (player count, seed, difficulty)
      game-loop.ts        # Async game loop + TUI state machine
      input.ts            # Global key handler, state-aware dispatch
      renderer/
        table.ts          # Player positions around oval, cards, coins
        center.ts         # Center action display box
        hand.ts           # Your hand (large cards at bottom)
        history.ts        # History ticker (ScrollBox, 3 lines)
        actions.ts        # Action panel: grid layout, selection cursor
        target.ts         # Target selection: cursor on table, spatial nav
        game-over.ts      # Game over screen
      animation/
        index.ts          # Animation scheduler (queue + Timeline integration)
        challenge.ts      # Challenge reveal: ▓▓ → [??] → [Dk]
        death.ts          # Card death: flash red → dim gray
        coins.ts          # Coin change: flash green/red → settle
        pulse.ts          # Card pulse during LOSE_CARD / EXCHANGE
        spinner.ts        # Bot thinking: braille spinner
        fade.ts           # History entry fade-in
      layout.ts           # Oval geometry: N players → (x,y) positions
      constants.ts        # COLORS palette, CARD_SYMBOLS, CARD_ABBREV
    package.json
    tsconfig.json
    build.sh              # Compile libcoup + bun run
```

### Task DAG

20 tasks, organized so 5–10 run in parallel per wave.

```
Wave 1 (all independent — 8 agents) ✅ ALL DONE
┌─────────────────────────────────────────────────────────────────┐
│ T1  Scaffolding ✅        T2  OpenTUI spike ✅                   │
│ T3  Shared types ✅       T4  C FFI raw bindings ✅              │
│ T5  Agent interface ✅    T6  History renderer (pure fns) ✅     │
│ T7  Layout geometry ✅    T8  Constants & palette ✅             │
└─────────────────────────────────────────────────────────────────┘
                              │
Wave 2 (depends on Wave 1 — 9 agents) ✅ ALL DONE
┌─────────────────────────────────────────────────────────────────┐
│ T9  CoupGame wrapper ✅                                         │
│ T10 Bot: Easy ✅          T11 Bot: Medium ✅                    │
│ T12 Bot: Hard ✅          T13 ONNX stub ✅                      │
│ T14 Table renderer ✅                                           │
│ T15 Center display ✅                                           │
│ T16 Hand renderer ✅                                            │
│ T17 Animation primitives ✅                                     │
└─────────────────────────────────────────────────────────────────┘
                              │
Wave 3 (depends on Waves 1+2 — 7 agents) ✅ ALL DONE
┌─────────────────────────────────────────────────────────────────┐
│ T18 Action panel + grid nav ✅                                  │
│ T19 History ticker component ✅                                 │
│ T20 Setup screen ✅                                             │
│ T21 Game over screen ✅                                         │
│ T22 Target selection (integrated into T18/T25) ✅               │
│ T23 Bot turn display (integrated into T25) ✅                   │
│ T24 Input handler / state dispatch ✅                           │
└─────────────────────────────────────────────────────────────────┘
                              │
Wave 4 (integration — 5 agents) ✅ ALL DONE
┌─────────────────────────────────────────────────────────────────┐
│ T25 Game loop + state machine ✅                                │
│ T26 CLI entry point + arg parsing ✅                            │
│ T27 Build script (compile C + bun run) ✅                       │
│ T28 Integration tests (21 tests passing) ✅                     │
│ T29 Polish: resize, edge cases, fast mode ✅                    │
└─────────────────────────────────────────────────────────────────┘
```

### Task Details

#### Wave 1 — Foundation (8 agents, all independent) — ALL DONE

**T1: Scaffolding** — DONE
- Create bun workspace root with `packages/game-client/` and `tui/`
- `package.json` for each with `@opentui/core` dep in tui
- `tsconfig.json` with shared base config, strict mode
- `.gitignore` for build artifacts, `*.dylib`, `*.so`, `node_modules`
- Output: both packages install and `bun run` doesn't crash

**T2: OpenTUI Spike** — DONE
- Install `@opentui/core`, create a throwaway test app
- Validate: FrameBuffer creation, direct cell writes, RGBA.fromHex, drawText
- Validate: KeyHandler with arrow keys, enter, escape, q
- Validate: Timeline or setTimeout-based animation at 100-200ms intervals
- Validate: Box, Text, Select components render correctly
- Output: working spike + notes on any API surprises. Delete spike after, keep learnings in code comments

**T3: Shared Types** — DONE
- `packages/game-client/src/types.ts`
- Enums: `CardType` (Duke/Assassin/Captain/Ambassador/Contessa), `ActionType` (0-31 flat space matching plan.md §4), `Phase` (matching C engine phase enum)
- Player state type: `{ coins, influence, cards: [CardState, CardState], alive }`
- Game snapshot type for TUI consumption
- Export everything, no runtime deps

**T4: C FFI Raw Bindings** — DONE
- `packages/game-client/src/ffi.ts`
- `dlopen` with platform detection (`.dylib` vs `.so`)
- All function signatures matching C engine headers
- Buffer allocation helpers (game struct, observation tensor)
- Smoke test: can call `game_init` without segfault
- NOTE: will need adjustment when C engine is finalized — code against expected API shape from plan.md

**T5: Agent Interface** — DONE
- `packages/game-client/src/agent.ts`
- `interface Agent { chooseAction(obs: Float32Array, validMask: number): Promise<number>; name: string; }`
- `AgentRegistry` class: register agent factories by name, create by difficulty string
- `HumanAgent` placeholder (resolves via external callback — TUI provides user input)

**T6: History Renderer** — DONE
- `packages/game-client/src/history.ts`
- Pure functions: `renderEvent(event: GameEvent): string`
- Event types: Income, ForeignAid, Tax, Steal, Assassinate, Coup, Exchange, Challenge, Block, LoseCard, Elimination
- Example: `"bot-2 claims Duke for Tax. No challenge. +3 coins."`
- No UI dependency — just string generation

**T7: Layout Geometry** — DONE
- `tui/src/layout.ts`
- `computePlayerPositions(playerCount: number, termWidth: number, termHeight: number): PlayerPosition[]`
- Oval arrangement per §3: you at bottom center, others clockwise
- Handle 2-6 players with the specific layouts shown in the plan
- Spatial navigation map: given current position + direction, return next valid position (skip dead players)
- Pure math, no rendering

**T8: Constants & Palette** — DONE
- `tui/src/constants.ts`
- `COLORS` object matching §10 palette (all hex strings)
- `CARD_ABBREV`: Duke→"Dk", Assassin→"As", Captain→"Cp", Ambassador→"Am", Contessa→"Ct"
- `CARD_SYMBOLS`: Duke→"♦♦♦", Assassin→"†††", Captain→"⚓⚓⚓", Ambassador→"✦✦✦", Contessa→"♥♥♥"
- `CARD_COLORS`: per-card-type color from COLORS
- Key bindings constants

#### Wave 2 — Engine + Rendering (9 agents) — ALL DONE

**T9: CoupGame Wrapper** — DONE [T3, T4]
- `packages/game-client/src/game.ts`
- Class wrapping FFI calls with typed TS API
- Methods: `init(seed)`, `step(action)`, `getValidMask()`, `observe(player)`, `isOver`, `winner`, `activePlayer`, `phase`
- Convenience: `getPlayerState(id)` → coins, influence, card states
- `getSnapshot()` → full typed game state for TUI rendering
- Handles buffer lifecycle

**T10: Bot Easy** — DONE [T5]
- `packages/game-client/src/agents/heuristic-easy.ts`
- Uniform random from valid actions
- Never challenges, never blocks (pass on all challenge/block phases)

**T11: Bot Medium** — DONE [T5, T3]
- `packages/game-client/src/agents/heuristic-medium.ts`
- Rule-based per the tier spec above
- Needs to interpret observation tensor to know own cards, coin counts, influence counts

**T12: Bot Hard** — DONE [T5, T3]
- `packages/game-client/src/agents/heuristic-hard.ts`
- Belief tracking from observation history
- Probabilistic bluffing and challenge decisions
- Adaptive targeting based on game state

**T13: ONNX Stub** — DONE [T5]
- `packages/game-client/src/agents/onnx-stub.ts`
- Implements Agent interface
- Constructor takes model path, loads via `onnxruntime-node` (optional dep)
- Falls back to random if model not found
- Actual inference pipeline ready, just no real model

**T14: Table Renderer** — DONE [T1, T2, T8, T7]
- `tui/src/renderer/table.ts`
- Renders all non-human players in oval positions on FrameBuffer
- Card display: `▓▓` (hidden), `[Dk]` (revealed), per §3 rules
- Alive indicator: `●` / `○` / `☠`
- Coins: `N●`
- Player name + bot label
- Active player highlight (bright border/name)
- Accepts a `GameSnapshot` and redraws

**T15: Center Display** — DONE [T1, T2, T8]
- `tui/src/renderer/center.ts`
- Bordered box in the center of the table area
- Content driven by game phase: claim text, waiting text, resolution text
- Methods: `showAction(text)`, `showWaiting()`, `showResolution(text)`, `clear()`

**T16: Hand Renderer** — DONE [T1, T2, T8]
- `tui/src/renderer/hand.ts`
- Large card boxes at bottom center per §3
- Card name + symbol inside bordered box
- Per-card-type coloring
- Dim/dashed style for dead cards
- Coin display below cards

**T17: Animation Primitives** — DONE [T1, T2, T8]
- `tui/src/animation/` — all files
- Animation scheduler: queue animations, play sequentially or concurrently
- Implement all 6 animation types from §6:
  - Challenge reveal (400ms, 4 frames)
  - Card death (300ms, 4 frames)
  - Coin change (300ms, 4 frames)
  - Card pulse (400ms toggle while selecting)
  - History fade-in (500ms, 3 frames)
  - Bot spinner (200ms cycle, braille characters)
- Each animation is an async function that mutates FrameBuffer cells + awaits delays
- Respect `--fast` flag: skip all delays

#### Wave 3 — Interaction (7 agents) — ALL DONE

**T18: Action Panel** — DONE [T1, T2, T8]
- `tui/src/renderer/actions.ts`
- Grid layout: actions in 3 columns, `▸` cursor
- Arrow key navigation: ←→ between columns, ↑↓ between rows, wrap at edges
- Dynamic grid: only show valid actions (filter by mask + coin count)
- Different layouts per phase: MAIN_ACTION (grid), CHALLENGE (horizontal), BLOCK (2×2), LOSE_CARD (horizontal), EXCHANGE_DISCARD (horizontal with labels)
- Returns selected action index via Promise

**T19: History Ticker** — DONE [T1, T2, T6]
- `tui/src/renderer/history.ts`
- 3-line area between table and action panel
- Auto-scroll to latest entry
- New entries trigger fade-in animation (from T17)
- Accepts string from history renderer (T6)

**T20: Setup Screen** — DONE [T1, T2]
- `tui/src/setup.ts`
- Full-screen config per §2: Players (2-6), Your seat, Bot level, Seed
- ↑↓ navigate fields, ←→ adjust values, Enter on "Start Game"
- Input component for seed field
- Returns config object: `{ players, seat, difficulty, seed }`

**T21: Game Over Screen** — DONE [T1, T2, T8]
- `tui/src/renderer/game-over.ts`
- Results display per §10: winner, elimination order, seed, turn count
- Key handlers: R (replay), N (new game), Q (quit)
- Returns user choice

**T22: Target Selection** — DONE [T14, T18, T7]
- `tui/src/renderer/target.ts`
- `▶` cursor on table pointing at valid targets
- Spatial navigation using layout geometry (T7): ↑↓←→ move around oval, skip dead players
- Sync between table cursor and action panel list
- Returns selected player index via Promise

**T23: Bot Turn Display** — DONE [T15, T17]
- Flow controller for bot turns per §7
- Sequence: spinner (300-600ms) → center update → challenge round → block round → resolution animation → history append → pause (200ms)
- Pauses for human response when player has challenge/block opportunity
- Respects `--fast` flag

**T24: Input Handler** — DONE [T1, T2]
- `tui/src/input.ts`
- Global KeyHandler: dispatches to current phase handler
- `q` → quit confirmation dialog
- Arrow keys + Enter → routed to active panel (action/target/setup/game-over)
- Number keys 1-9 as shortcuts
- ESC → back (in target select) or no-op (at top level)
- State-aware: only active panel receives input

#### Wave 4 — Integration (5 agents) — ALL DONE

**T25: Game Loop + State Machine** — DONE [T9, T10-12, T14-T24]
- `tui/src/game-loop.ts`
- Async state machine per §4: IDLE → ACTION_SELECT → TARGET_SELECT → … → GAME_OVER
- Human turn: show action panel, await input, step engine, play animations
- Bot turn: run bot turn display sequence (T23)
- Challenge/block opportunities for human during bot turns
- Wire all renderers: table, center, hand, history, actions, target
- Coordinate animation queue with game progression

**T26: CLI Entry Point** — DONE [T20, T25]
- `tui/src/index.ts`
- Parse CLI args: `--players`, `--seed`, `--difficulty`, `--fast`, `--seat`
- If args provided: skip setup, launch game directly
- If no args: show setup screen, then launch game
- Handle game-over choices: replay (same seed), new game (back to setup), quit

**T27: Build Script** — DONE [T4, T25]
- `tui/build.sh`
- Compile C engine to shared library (platform-detect macOS/Linux)
- Verify library loads via quick FFI smoke test
- `bun run tui/src/index.ts "$@"`
- Also: `package.json` scripts for `bun run dev`, `bun run build`

**T28: Integration Tests** — DONE [T9, T10-12, T25]
- `tui/tests/`
- Test CoupGame wrapper: init → step sequence → game over → winner
- Test each heuristic bot: plays a full game without crashing, returns valid actions
- Test history renderer: known events → expected strings
- Test layout geometry: N=2,3,4,5,6 → valid positions, spatial nav correctness
- Test action masking: correct actions shown per phase + coin count
- Run with `bun test`

**T29: Polish** — DONE [T25]
- Terminal resize handler: recompute layout, full redraw
- Minimum terminal size check (80×24), show warning if too small
- Seed display in corner (§10)
- `--fast` flag wired through all animation + bot delays
- Edge cases: 2-player game, last player standing, forced coup at 10 coins
- Clean shutdown: restore terminal state on exit/crash

### Execution Protocol

Each task is assigned to one subagent. Agents receive:
1. This plan (relevant task + dependencies)
2. Output from completed dependency tasks (file paths, API shapes)
3. The relevant section(s) of the TUI plan above for visual/behavioral spec

Rules:
- **Do not modify files owned by another task** — coordinate via the typed interfaces in `types.ts` and `agent.ts`
- **Wave N+1 agents do not start until all of Wave N is complete** — except when a task's specific deps are all done (can start early)
- **Each agent runs `bun run` or `bun test` before marking done** — code must execute, not just compile
- **Use `bun` for all package management** — no npm, no yarn
- **Assume C engine exists at `c_engine/libcoup.dylib`** — if not present, FFI tests should skip gracefully with a clear message
