# Coup: Web Multiplayer Deployment Plan

> **Note (2026-04-02):** This plan is deferred — not in current dev scope. Path references to `shared/coup_core.c` should read `c_engine/coup_core.c` per the updated repo structure. See `dev-plan.md` and `plans/plan.md` for current architecture decisions.

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [WASM Compilation](#3-wasm-compilation)
4. [Cloudflare Infrastructure](#4-cloudflare-infrastructure)
5. [Client-Server Protocol](#5-client-server-protocol)
6. [Bot Implementation](#6-bot-implementation)
7. [Offline Mode](#7-offline-mode)
8. [Frontend](#8-frontend)
9. [Deployment](#9-deployment)
10. [Cost](#10-cost)
11. [Implementation Order](#11-implementation-order)

---

## 1. Overview

Players visit a website, type a username, and either create a lobby (with a shareable room code), join an existing one, quick-match into a random game, or play offline against AI. Games run with any mix of humans and bot opponents.

The core C sim compiles to WASM via Emscripten and runs in two places: inside a Cloudflare Durable Object (server-authoritative multiplayer) and in the browser (offline play against bots). No TypeScript port of the game engine is needed — the same C source targets native, WASM, and C++ (OpenSpiel) with identical behavior and PRNG sequences.

**Stack summary:**

- **Game engine:** C → WASM (Emscripten)
- **Server:** Cloudflare Workers + Durable Objects (WebSocket relay + authoritative sim)
- **Frontend:** React/Next.js (or plain HTML) on Cloudflare Pages or Vercel
- **Bot inference:** Heuristic bot (v1), ONNX Runtime WASM (v2, server-side), ONNX Runtime Web + WebGPU (v2, client-side offline)
- **No containers, no VMs, no custom servers**

---

## 2. Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                        Cloudflare Edge                         │
│                                                                │
│  ┌──────────────────┐     ┌──────────────────────────────┐    │
│  │  Matchmaker DO    │     │  Game DO (one per room)       │    │
│  │  (singleton)      │     │                                │    │
│  │                   │     │  ┌────────────────────────┐   │    │
│  │  Waiting queue    │────▶│  │  coup.wasm              │   │    │
│  │  Room creation    │     │  │  (C sim, authoritative)  │   │    │
│  │  Quick match      │     │  └────────────────────────┘   │    │
│  └──────────────────┘     │  PRNG state (owned by server)  │    │
│                            │  Action history (for reconnect) │    │
│                            │  Bot logic (heuristic or ONNX) │    │
│                            │  WebSocket per human player     │    │
│                            └──────────────────────────────────┘    │
└────────────────────────────────────────────────────────────────┘
         ▲                              ▲
         │ WS: join queue               │ WS: send action (1 int),
         │     preferences              │     receive observation (JSON)
         ▼                              ▼
┌────────────────────────────────────────────────────────────────┐
│                     Browser (client)                            │
│                                                                │
│  Multiplayer mode:          Offline mode:                      │
│  ├── Renders observation    ├── coup.wasm (local sim)          │
│  ├── Shows available acts   ├── policy.onnx (WebGPU inference) │
│  ├── Sends action integer   └── Runs entirely client-side      │
│  └── No game logic                                             │
└────────────────────────────────────────────────────────────────┘
```

**Key design decision: server-authoritative for multiplayer.** Coup is an incomplete information game. If the client ran the full sim, a player could open devtools and read everyone's cards. The server runs the sim and sends each player only their own observation. The client is a renderer that sends back one integer per decision. For offline play against bots, information leaking doesn't matter, so the full sim runs client-side.

**One C codebase, four compilation targets, zero ports:**

```
shared/coup_core.c + prng.h
    ├── gcc/clang → native binary       (tests, PufferLib training)
    ├── g++ link  → OpenSpiel .so       (CFR, exploitability)
    ├── emcc      → coup.wasm (server)  (Cloudflare Durable Object)
    └── emcc      → coup.wasm (client)  (browser offline mode)
```

---

## 3. WASM Compilation

```bash
emcc shared/coup_core.c shared/text_render.c \
  -O3 \
  -s EXPORTED_FUNCTIONS='[
    "_game_init",
    "_step_deterministic",
    "_step_with_rng",
    "_get_valid_actions",
    "_observe",
    "_render_text",
    "_is_done",
    "_get_winner",
    "_get_active_player",
    "_get_phase",
    "_chance_outcomes",
    "_apply_chance",
    "_is_chance_node"
  ]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPF32","HEAP8","_malloc","_free"]' \
  -s ALLOW_MEMORY_GROWTH=0 \
  -s INITIAL_MEMORY=65536 \
  -s STANDALONE_WASM=0 \
  -o coup.wasm
```

Memory is tiny — the sim needs a few hundred bytes at most. `ALLOW_MEMORY_GROWTH=0` avoids any reallocation overhead.

**Verification:** after compiling, run the determinism test — seed 42, always pick first legal action, compare action sequence and final outcome to native C output. If they match, the WASM build is correct. The PRNG produces identical sequences because it's the same C source.

---

## 4. Cloudflare Infrastructure

### Matchmaker Durable Object (Singleton)

One global instance. Manages the player queue and room creation.

```typescript
export class Matchmaker implements DurableObject {
  private queue: Map<string, { ws: WebSocket; username: string; preferences: RoomPrefs }>;

  async fetch(request: Request): Promise<Response> {
    const { 0: client, 1: server } = new WebSocketPair();
    this.ctx.acceptWebSocket(server);
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, msg: string) {
    const data = JSON.parse(msg);

    switch (data.type) {
      case "quick_play":
        // Add to queue with preferences (player count, etc.)
        this.queue.set(data.username, { ws, username: data.username, preferences: data.prefs });
        this.tryMatch();
        break;

      case "create_room":
        // Generate 6-char room code, create Game DO
        const code = generateRoomCode();
        const gameId = this.env.GAME.idFromName(code);
        ws.send(JSON.stringify({ type: "room_created", code }));
        break;

      case "join_room":
        // Look up Game DO by code, send WS URL
        ws.send(JSON.stringify({ type: "join", code: data.code, url: gameUrl(data.code) }));
        break;
    }
  }

  private tryMatch() {
    // Group queued players by compatible preferences
    // When enough players found, create Game DO, notify all
    // ...
  }
}
```

### Game Durable Object (One Per Room)

Runs the WASM sim. Owns all state and randomness. Handles human WebSocket connections and bot turns internally.

```typescript
export class GameRoom implements DurableObject {
  private wasm: WasmInstance;          // Compiled coup.wasm
  private gamePtr: number;             // Pointer to Game struct in WASM memory
  private players: PlayerSlot[];       // [{username, ws, seat, isBot, connected}]
  private actionHistory: number[];     // For reconnection replay
  private config: RoomConfig;          // {numPlayers, numBots, botDifficulty}

  constructor(state: DurableObjectState, env: Env) {
    this.wasm = await loadWasm();
    this.gamePtr = this.wasm._malloc(32);  // 32-byte Game struct
  }

  // --- Player connection ---

  async webSocketMessage(ws: WebSocket, msg: string) {
    const data = JSON.parse(msg);
    const seat = this.seatForSocket(ws);

    switch (data.type) {
      case "join":
        this.assignSeat(ws, data.username);
        if (this.allSeatsReady()) this.startGame();
        break;

      case "action":
        if (!this.isActivePlayer(seat)) return;  // Not your turn
        const mask = this.wasm._get_valid_actions(this.gamePtr);
        if (!((mask >> data.action) & 1)) return;  // Invalid action

        this.applyAction(data.action);
        this.resolveChanceAndBots();
        this.broadcastObservations();
        break;
    }
  }

  // --- Game logic ---

  private startGame() {
    const seed = crypto.getRandomValues(new BigUint64Array(1))[0];
    this.wasm._game_init(this.gamePtr, Number(seed >> 32n), Number(seed & 0xFFFFFFFFn));

    // Resolve initial deal (chance nodes)
    this.resolveChanceAndBots();
    this.broadcastObservations();
  }

  private applyAction(action: number) {
    this.wasm._step_deterministic(this.gamePtr, action);
    this.actionHistory.push(action);
  }

  private resolveChanceAndBots() {
    // Resolve any pending chance nodes (deal, redraw, exchange draw)
    while (this.wasm._is_chance_node(this.gamePtr)) {
      // Read chance outcomes, sample using server-side PRNG
      const outcome = this.sampleChance();
      this.wasm._apply_chance(this.gamePtr, outcome);
      this.actionHistory.push(outcome);  // For replay
    }

    // If active player is a bot, compute and apply their action
    while (!this.wasm._is_done(this.gamePtr) && this.isBot(this.getActivePlayer())) {
      const action = this.computeBotAction();
      this.applyAction(action);

      // Resolve any chance nodes triggered by bot action
      while (this.wasm._is_chance_node(this.gamePtr)) {
        const outcome = this.sampleChance();
        this.wasm._apply_chance(this.gamePtr, outcome);
        this.actionHistory.push(outcome);
      }
    }

    // Check for game over
    if (this.wasm._is_done(this.gamePtr)) {
      this.broadcastGameOver();
    }
  }

  // --- Observations ---

  private broadcastObservations() {
    const obsPtr = this.wasm._malloc(410 * 4);  // 410 floats

    for (const player of this.players) {
      if (player.isBot || !player.connected) continue;

      this.wasm._observe(this.gamePtr, player.seat, obsPtr);

      // Build JSON observation from WASM memory
      const obs = this.buildObservationJSON(player.seat, obsPtr);
      player.ws.send(JSON.stringify(obs));
    }

    this.wasm._free(obsPtr);
  }

  private buildObservationJSON(seat: number, obsPtr: number): GameObservation {
    const mask = this.wasm._get_valid_actions(this.gamePtr);
    const activePlayer = this.wasm._get_active_player(this.gamePtr);
    const phase = this.wasm._get_phase(this.gamePtr);

    return {
      your_seat: seat,
      your_cards: this.getPlayerCards(seat),
      players: this.getPublicPlayerInfo(),
      phase: PHASE_NAMES[phase],
      active_player: activePlayer,
      is_your_turn: activePlayer === seat,
      available_actions: activePlayer === seat ? this.maskToActions(mask) : [],
      history: this.getReadableHistory(),
      game_over: false,
    };
  }

  // --- Reconnection ---

  private reconnectPlayer(ws: WebSocket, seat: number) {
    // Replay action history to rebuild WASM state
    // (or just store a WASM memory snapshot and restore)
    // Then send current observation
    this.players[seat].ws = ws;
    this.players[seat].connected = true;
    this.broadcastObservationToSeat(seat);
  }

  // --- Disconnection ---

  async webSocketClose(ws: WebSocket) {
    const seat = this.seatForSocket(ws);
    this.players[seat].connected = false;

    // Hold seat for 60 seconds
    setTimeout(() => {
      if (!this.players[seat].connected) {
        this.players[seat].isBot = true;  // Replace with bot
        if (this.isActivePlayer(seat)) {
          // It was their turn — bot takes over immediately
          this.resolveChanceAndBots();
          this.broadcastObservations();
        }
      }
    }, 60000);
  }
}
```

### Worker Router

Routes incoming requests to the appropriate Durable Object.

```typescript
export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/matchmaker") {
      const id = env.MATCHMAKER.idFromName("global");
      const stub = env.MATCHMAKER.get(id);
      return stub.fetch(request);
    }

    if (url.pathname.startsWith("/game/")) {
      const code = url.pathname.split("/")[2];
      const id = env.GAME.idFromName(code);
      const stub = env.GAME.get(id);
      return stub.fetch(request);
    }

    return new Response("Not found", { status: 404 });
  }
};
```

### wrangler.toml

```toml
name = "coup-server"
main = "src/index.ts"
compatibility_date = "2024-01-01"

[durable_objects]
bindings = [
  { name = "MATCHMAKER", class_name = "Matchmaker" },
  { name = "GAME", class_name = "GameRoom" }
]

[[migrations]]
tag = "v1"
new_classes = ["Matchmaker", "GameRoom"]
```

---

## 5. Client-Server Protocol

### WebSocket Messages: Client → Server

```typescript
// Join a game room
{ type: "join", username: "alice" }

// Submit an action (one integer)
{ type: "action", action: 2 }

// Reconnect to existing game
{ type: "reconnect", username: "alice", seat: 3 }
```

### WebSocket Messages: Server → Client

```typescript
// Waiting for players
{
  type: "lobby",
  code: "ABCD12",
  players: [
    { username: "alice", seat: 0, is_bot: false, ready: true },
    { username: "Bot (Medium)", seat: 1, is_bot: true, ready: true },
    null, null, null, null  // empty seats
  ]
}

// Game state update (sent after every action resolution)
{
  type: "state",
  your_seat: 0,
  your_cards: [
    { type: "duke", alive: true },
    { type: "captain", alive: true }
  ],
  players: [
    { name: "alice", coins: 5, influences: 2, revealed: [], is_bot: false },
    { name: "Bot (Medium)", coins: 3, influences: 1, revealed: ["contessa"], is_bot: true },
    { name: "bob", coins: 7, influences: 2, revealed: [], is_bot: false },
    { name: "ELIMINATED", coins: 0, influences: 0, revealed: ["duke", "assassin"], is_bot: false },
    { name: "charlie", coins: 4, influences: 2, revealed: [], is_bot: false },
    { name: "Bot (Hard)", coins: 2, influences: 1, revealed: ["ambassador"], is_bot: true }
  ],
  phase: "main_action",
  active_player: 0,
  is_your_turn: true,
  available_actions: [
    { id: 0, label: "Income (take 1 coin)" },
    { id: 1, label: "Foreign Aid (take 2 coins)" },
    { id: 2, label: "Tax (claim Duke, take 3 coins)" },
    { id: 3, label: "Exchange (claim Ambassador)" },
    { id: 10, label: "Steal from Bot (Medium) (claim Captain)" },
    { id: 12, label: "Steal from bob (claim Captain)" },
    { id: 14, label: "Steal from charlie (claim Captain)" },
    { id: 15, label: "Steal from Bot (Hard) (claim Captain)" },
    { id: 16, label: "Assassinate Bot (Medium) (claim Assassin, pay 3 coins)" },
    { id: 18, label: "Assassinate bob (claim Assassin, pay 3 coins)" },
    { id: 20, label: "Assassinate charlie (claim Assassin, pay 3 coins)" },
    { id: 21, label: "Assassinate Bot (Hard) (claim Assassin, pay 3 coins)" }
  ],
  history: [
    { text: "bob claimed Duke for Tax. No one challenged. Gained 3 coins.", turn: 3 },
    { text: "Bot (Hard) took Income. Gained 1 coin.", turn: 4 }
  ]
}

// Challenge/block prompt (same structure, different phase + actions)
{
  type: "state",
  phase: "challenge_action",
  is_your_turn: true,
  available_actions: [
    { id: 22, label: "Challenge" },
    { id: 23, label: "Pass" }
  ],
  context: "bob claims Captain to Steal from you."
  // ... rest of state fields
}

// Game over
{
  type: "game_over",
  winner: 2,
  winner_name: "bob",
  final_standings: [
    { seat: 0, name: "alice", place: 3, eliminated_turn: 12 },
    { seat: 1, name: "Bot (Medium)", place: 4, eliminated_turn: 8 },
    { seat: 2, name: "bob", place: 1, eliminated_turn: null },
    { seat: 3, name: "dave", place: 5, eliminated_turn: 6 },
    { seat: 4, name: "charlie", place: 2, eliminated_turn: 18 },
    { seat: 5, name: "Bot (Hard)", place: 6, eliminated_turn: 3 }
  ]
}
```

**Key property:** the server never sends hidden information. A player's observation contains their own cards, everyone's public info (coins, revealed cards, alive status), the action history, and the valid action list for the current decision. Nothing more.

---

## 6. Bot Implementation

### V1: Heuristic Bot (~150 lines TypeScript, runs in Durable Object)

```typescript
function heuristicBotAction(obs: BotObservation, difficulty: "easy" | "medium"): number {
  const { myCards, myCoins, validMask, phase, players, pendingAction } = obs;

  if (difficulty === "easy") {
    // Random legal action
    const legal = bitsToArray(validMask);
    return legal[Math.floor(Math.random() * legal.length)];
  }

  // --- Medium difficulty ---

  if (phase === Phase.MAIN_ACTION) {
    // Must coup at 10+ (mask enforces this, but be explicit)
    if (myCoins >= 10) return pickCoupTarget(players, validMask);

    // Coup if >= 7 and someone has 1 influence
    if (myCoins >= 7) {
      const weakTarget = findWeakTarget(players, validMask);
      if (weakTarget >= 0) return 4 + weakTarget;  // coup target
    }

    // Play honestly when possible
    if (hasCard("duke") && isValid(validMask, 2)) return 2;        // tax
    if (hasCard("assassin") && myCoins >= 3) {
      const target = findBestAssassinTarget(players, validMask);
      if (target >= 0) return 16 + target;
    }
    if (hasCard("captain")) {
      const target = findRichestStealTarget(players, validMask);
      if (target >= 0) return 10 + target;
    }

    // Occasional bluffs
    if (Math.random() < 0.15 && isValid(validMask, 2)) return 2;  // bluff tax

    // Default: income
    return 0;
  }

  if (phase === Phase.CHALLENGE_ACTION || phase === Phase.CHALLENGE_BLOCK) {
    // Challenge if we know they're lying (card is revealed/in our hand)
    if (canDisprove(myCards, players, pendingAction)) return 22;
    // Occasionally bluff-challenge
    if (Math.random() < 0.1) return 22;
    return 23;  // pass
  }

  if (phase === Phase.BLOCK) {
    // Block honestly if we have the right card
    const blockAction = findHonestBlock(myCards, pendingAction, validMask);
    if (blockAction >= 0) return blockAction;
    // Occasionally bluff-block if the stakes are high
    if (pendingAction === "assassinate" && Math.random() < 0.3) return 24;  // bluff contessa
    return 23;  // pass
  }

  if (phase === Phase.LOSE_CARD) {
    // Lose the less useful card
    return pickCardToLose(myCards, myCoins, validMask);
  }

  if (phase === Phase.EXCHANGE_DISCARD) {
    return pickExchangeDiscard(myCards, validMask);
  }

  // Fallback: first legal action
  return firstSetBit(validMask);
}
```

### V2: Neural Bot (ONNX, added later)

Export the trained PPO policy from PyTorch to ONNX:

```python
torch.onnx.export(
    policy_network,
    dummy_obs,
    "policy.onnx",
    input_names=["observation"],
    output_names=["logits", "value"],
    dynamic_axes={"observation": {0: "batch"}}
)
```

Server-side (Durable Object): load via ONNX Runtime WASM build. Client-side (offline mode): load via ONNX Runtime Web with WebGPU backend. Same model file, two runtimes.

```typescript
// Server-side (Durable Object)
import * as ort from 'onnxruntime-web/wasm';

const session = await ort.InferenceSession.create('./policy.onnx');

function neuralBotAction(observation: Float32Array, validMask: number): number {
  const input = new ort.Tensor('float32', observation, [1, 410]);
  const results = await session.run({ observation: input });
  const logits = results.logits.data as Float32Array;

  // Apply mask
  for (let i = 0; i < 32; i++) {
    if (!((validMask >> i) & 1)) logits[i] = -1e8;
  }

  // Sample from softmax
  return sampleFromLogits(logits);
}
```

### Bot Difficulty Tiers

| Tier | Implementation | Where it runs |
|------|---------------|---------------|
| Easy | Random legal action | Durable Object |
| Medium | Heuristic (honest play + occasional bluffs) | Durable Object |
| Hard | Trained PPO policy via ONNX | Durable Object (WASM) or Browser (WebGPU) |

---

## 7. Offline Mode

The browser loads the WASM sim and the ONNX model directly. No server communication. The player configures the game (number of bots, difficulty) and plays entirely client-side.

```typescript
// offline-engine.ts

import { loadWasm } from './coup-wasm';
import * as ort from 'onnxruntime-web';

class OfflineGame {
  private wasm: WasmInstance;
  private gamePtr: number;
  private session: ort.InferenceSession | null;
  private botSeats: Map<number, BotConfig>;

  async init(config: OfflineConfig) {
    this.wasm = await loadWasm('/coup.wasm');
    this.gamePtr = this.wasm._malloc(32);

    if (config.hardBots > 0) {
      this.session = await ort.InferenceSession.create('/policy.onnx', {
        executionProviders: ['webgpu', 'wasm']  // fallback chain
      });
    }

    const seed = BigInt(Date.now());
    this.wasm._game_init(this.gamePtr, Number(seed >> 32n), Number(seed & 0xFFFFFFFFn));

    // Resolve deal
    this.resolveChanceNodes();
  }

  getObservation(seat: number): GameObservation {
    // Read from WASM, build JSON
  }

  submitAction(action: number) {
    this.wasm._step_deterministic(this.gamePtr, action);
    this.resolveChanceNodes();
    this.runBotTurns();
  }

  private resolveChanceNodes() {
    while (this.wasm._is_chance_node(this.gamePtr)) {
      const outcome = this.sampleChance();
      this.wasm._apply_chance(this.gamePtr, outcome);
    }
  }

  private async runBotTurns() {
    while (!this.wasm._is_done(this.gamePtr) && this.isBot(this.getActivePlayer())) {
      const seat = this.getActivePlayer();
      const config = this.botSeats.get(seat);
      const action = config.difficulty === "hard"
        ? await this.neuralBotAction(seat)
        : this.heuristicBotAction(seat, config.difficulty);
      this.wasm._step_deterministic(this.gamePtr, action);
      this.resolveChanceNodes();
    }
  }
}
```

Since it's single-player, information leaking is irrelevant — the player can inspect WASM memory if they want. The full sim state is present client-side.

---

## 8. Frontend

### File Structure

```
site/
├── pages/
│   ├── index.tsx              # Landing: username, play options
│   ├── lobby/[code].tsx       # Waiting room
│   └── game/[code].tsx        # Active game
├── components/
│   ├── UsernameInput.tsx      # Username entry + cookie persistence
│   ├── MainMenu.tsx           # Create / Join / Quick Play / Offline buttons
│   ├── LobbyView.tsx          # Player list, bot config, start button
│   ├── GameBoard.tsx          # Main game layout
│   ├── PlayerCard.tsx         # One player's public info (coins, influences, revealed)
│   ├── HandDisplay.tsx        # Your own cards (private)
│   ├── ActionPicker.tsx       # Buttons for available actions
│   ├── HistoryLog.tsx         # Scrollable action history
│   ├── PhaseIndicator.tsx     # "Waiting for Player 2 to respond..."
│   └── GameOver.tsx           # Results, play again
├── lib/
│   ├── ws.ts                  # WebSocket client wrapper with reconnection
│   ├── offline-engine.ts      # WASM sim + bot inference for offline mode
│   └── types.ts               # Shared TypeScript types for protocol
├── public/
│   ├── coup.wasm              # Compiled C sim
│   └── policy.onnx            # Trained bot model (added in v2)
└── styles/
    └── ...
```

### User Flow

```
Landing Page
├── [Enter username] → stored in cookie
├── [Create Game] → POST to matchmaker → get room code
│   └── Lobby screen: share code, configure bots, wait for players
│       └── [Start] → all players redirect to game
├── [Join Game] → enter code → connect to Game DO
│   └── Lobby screen → wait for host to start
├── [Quick Play] → connect to Matchmaker DO queue
│   └── Waiting... → auto-matched → redirect to game
└── [Play vs AI] → configure bots locally
    └── Offline game (no server)
```

### Game Screen Layout

```
┌─────────────────────────────────────────────┐
│  Player 1 (alice)    Player 2 (Bot Medium)  │
│  ██ ██  5 coins      ██ [C]  3 coins        │
│                                              │
│  Player 3 (bob)      Player 4 (DEAD)        │
│  ██ ██  7 coins      [D] [A]  0 coins       │
│                                              │
│  Player 5 (charlie)  Player 6 (Bot Hard)    │
│  ██ ██  4 coins      ██ [Am]  2 coins       │
├─────────────────────────────────────────────┤
│  Your cards: [Duke ♦] [Captain ♦]           │
│  Your coins: 5                               │
├─────────────────────────────────────────────┤
│  [ Income ] [ Foreign Aid ] [ Tax ]         │
│  [ Exchange ] [ Steal → bob ] [ Coup → bob ]│
│  [ Assassinate → Bot Medium ] ...           │
├─────────────────────────────────────────────┤
│  History:                                    │
│  > bob claimed Duke for Tax. No challenge.  │
│  > Bot (Hard) took Income.                  │
│  > It's your turn.                          │
└─────────────────────────────────────────────┘
```

`██` = face-down card (hidden). `[C]` = revealed Contessa. `[D]` = revealed Duke. Actions shown as buttons, only valid ones rendered.

---

## 9. Deployment

### Build Pipeline

```bash
# 1. Compile WASM
emcc shared/coup_core.c shared/text_render.c \
  -O3 -o site/public/coup.wasm \
  -s EXPORTED_FUNCTIONS='[...]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPF32","HEAP8","_malloc","_free"]'

# 2. Run cross-platform determinism test
node tests/test_wasm_determinism.js  # Compare WASM output to native C

# 3. Deploy Cloudflare Workers (matchmaker + game DOs)
cd workers/
npx wrangler deploy

# 4. Deploy frontend
cd site/
npx vercel deploy          # if using Vercel
# or
npx wrangler pages deploy  # if using Cloudflare Pages
```

### Environment Configuration

```bash
# Workers secrets (if any, e.g. for analytics)
npx wrangler secret put ANALYTICS_KEY

# Custom domain (optional)
# Add DNS record pointing to workers route or pages project
```

---

## 10. Cost

### Cloudflare Workers + Durable Objects

| Resource | Free tier | Paid ($5/mo plan) | Coup usage per game |
|----------|-----------|-------------------|-------------------|
| Worker requests | 100k/day | 10M/mo | ~10 (routing) |
| DO requests | included | $0.15/M | ~300 (50 turns × 6 broadcasts) |
| DO duration | included | $0.0025/hr (wall clock) | ~0.15 hrs (10 min game) |
| DO storage | 1 GB | $0.20/GB | ~1 KB (action history) |

A game costs roughly: 300 DO requests ($0.000045) + 10 min wall clock ($0.000417) ≈ $0.0005 per game. **2,000 games per dollar.**

On the free tier: 100k requests/day ÷ 300 per game ≈ **333 concurrent games per day for free.** The $5/mo Workers plan gives 10M requests/month, supporting roughly 33,000 games/month.

### Frontend Hosting

Cloudflare Pages or Vercel free tier. Static site, no cost.

### Total for moderate usage (1,000 games/day)

Approximately $15–20/month on the Cloudflare paid plan. Negligible.

---

## 11. Implementation Order

**Phase 1: Foundation (1 week)**

1. Compile C sim to WASM. Verify determinism against native C.
2. Write the Game Durable Object: WASM loading, game lifecycle, WebSocket handling, observation JSON construction.
3. Test with `wscat` or a minimal HTML page that connects and sends raw JSON.

**Phase 2: Playable (1 week)**

4. Write the heuristic bot (medium difficulty).
5. Build minimal frontend: username entry, create/join room, game board with action buttons.
6. End-to-end test: two browser tabs playing against each other with bots filling remaining seats.

**Phase 3: Matchmaking + Polish (1 week)**

7. Build the Matchmaker DO: queue, quick play, room codes.
8. Add reconnection handling (replay action history on rejoin).
9. Add the "easy" bot tier (random actions).
10. Polish UI: animations for card reveals, turn indicators, challenge/block prompts.

**Phase 4: AI Bots (when trained policy is ready)**

11. Export PPO policy to ONNX.
12. Integrate ONNX Runtime WASM into the Durable Object for "Hard" bot tier.
13. Build offline mode: WASM sim + ONNX Runtime Web (WebGPU) in browser.

**Phase 5: Extras (as desired)**

14. Spectator mode (read-only WebSocket that sees public info only).
15. Game replays (store action histories, reconstruct and replay in browser).
16. Leaderboard / ELO (store results in Cloudflare KV or D1).
17. CLI/TUI client (connect to same WebSocket, render `text_render` output in terminal).
