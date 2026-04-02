// GameRoom Durable Object
// One instance per game. Runs the WASM sim, manages WebSocket connections,
// runs bot turns, broadcasts state.

import { CoupWasm } from "./wasm-bridge.js";
import { chooseBotAction, pickBotName } from "./bot.js";
import {
  type Env,
  type BotDifficulty,
  type HouseRules,
  type PlayerSlot,
  type ServerMessage,
  CARD_NAMES,
  PHASE_NAMES,
  PHASE_MAIN_ACTION,
  PHASE_CHALLENGE_ACTION,
  PHASE_BLOCK,
  PHASE_CHALLENGE_BLOCK,
  PHASE_LOSE_CARD,
  PHASE_EXCHANGE_DISCARD,
  ACTION_INCOME,
  ACTION_FOREIGN_AID,
  ACTION_TAX,
  ACTION_EXCHANGE,
  ACTION_CHALLENGE,
  ACTION_PASS,
  ACTION_BLOCK_CONTESSA,
  ACTION_BLOCK_CAPTAIN,
  ACTION_BLOCK_AMBASSADOR,
  ACTION_BLOCK_DUKE,
  ACTION_COUP_P0,
  ACTION_STEAL_P0,
  ACTION_ASSASSINATE_P0,
  ACTION_DISCARD_SLOT0,
  getValidActionsFromMask,
  actionLabel,
  actionTarget,
  claimedRole,
  NUM_ACTIONS,
} from "./types.js";

interface PlayerConn {
  ws: WebSocket;
  username: string;
  seat: number;
}

interface HistoryEntry {
  text: string;
  turn: number;
}

export class GameRoom {
  private state: DurableObjectState;
  private env: Env;

  // Room config
  private code = "";
  private numPlayers = 0;
  private numBots = 0;
  private botDifficulty: BotDifficulty = "medium";
  private houseRules: HouseRules = { refundOnChallenge: true };
  private initialized = false;
  private gameStarted = false;

  // Player management
  private players: PlayerSlot[] = [];
  private connections = new Map<WebSocket, PlayerConn>();
  private hostWs: WebSocket | null = null;
  private hostUsername: string | null = null;

  // Game engine
  private wasm: CoupWasm | null = null;
  private turnCounter = 0;
  private history: HistoryEntry[] = [];
  private pendingPasses: string[] = [];
  private claimedRoles = new Map<number, Set<string>>(); // seat → claimed role names
  private eliminationOrder: { seat: number; turn: number }[] = [];

  constructor(state: DurableObjectState, env: Env) {
    this.state = state;
    this.env = env;
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/init" && request.method === "POST") {
      return this.handleInit(request);
    }

    if (url.pathname === "/ws") {
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      server.accept();
      server.addEventListener("message", (event) => {
        this.handleWsMessage(server, event.data);
      });
      server.addEventListener("close", () => {
        this.handleWsClose(server);
      });
      server.addEventListener("error", () => {
        this.handleWsError(server);
      });
      return new Response(null, { status: 101, webSocket: client });
    }

    return new Response("Not found", { status: 404 });
  }

  private async handleInit(request: Request): Promise<Response> {
    if (this.initialized) {
      return new Response("Already initialized", { status: 409 });
    }

    const body = await request.json() as any;
    this.code = body.code;
    this.numPlayers = body.numPlayers;
    this.numBots = body.numBots;
    this.botDifficulty = body.botDifficulty || "medium";
    this.houseRules = { refundOnChallenge: true, ...body.houseRules };
    this.initialized = true;
    this.players = [];

    return new Response("OK", { status: 200 });
  }

  private async handleWsMessage(ws: WebSocket, data: string | ArrayBuffer): Promise<void> {
    if (typeof data !== "string") return;

    let msg: any;
    try {
      msg = JSON.parse(data);
    } catch {
      this.send(ws, { type: "error", message: "Invalid JSON" });
      return;
    }

    switch (msg.type) {
      case "join":
        this.handleJoin(ws, msg);
        break;
      case "start":
        await this.handleStart(ws);
        break;
      case "action":
        await this.handleAction(ws, msg);
        break;
      case "house_rules":
        this.handleHouseRules(ws, msg);
        break;
      case "forfeit":
        this.handleForfeit(ws);
        break;
      default:
        this.send(ws, { type: "error", message: `Unknown message type: ${msg.type}` });
    }
  }

  private handleWsClose(ws: WebSocket): void {
    const conn = this.connections.get(ws);
    if (conn) {
      this.connections.delete(ws);
      if (ws === this.hostWs) this.hostWs = null;
    }
  }

  private handleWsError(ws: WebSocket): void {
    this.connections.delete(ws);
    if (ws === this.hostWs) this.hostWs = null;
  }

  private handleHouseRules(ws: WebSocket, msg: any): void {
    if (ws !== this.hostWs) {
      this.send(ws, { type: "error", message: "Only host can change house rules" });
      return;
    }
    if (this.gameStarted) {
      this.send(ws, { type: "error", message: "Cannot change rules after game start" });
      return;
    }
    if (msg.houseRules && typeof msg.houseRules === "object") {
      this.houseRules = {
        refundOnChallenge: !!msg.houseRules.refundOnChallenge,
      };
      this.broadcastLobby();
    }
  }

  private handleForfeit(ws: WebSocket): void {
    const conn = this.connections.get(ws);
    if (!conn) return;

    if (!this.gameStarted) {
      this.send(ws, { type: "error", message: "No game in progress" });
      return;
    }

    const name = conn.username;

    // Clean up WASM
    if (this.wasm) {
      this.wasm.dispose();
      this.wasm = null;
    }

    // Broadcast forfeited to all players
    const msg: ServerMessage = { type: "forfeited", by: name };
    for (const [w] of this.connections) {
      this.send(w, msg);
    }

    // Fully reset room state — everyone will re-join from lobby
    this.gameStarted = false;
    this.history = [];
    this.turnCounter = 0;
    this.pendingPasses = [];
    this.claimedRoles.clear();
    this.eliminationOrder = [];
    this.players = [];
    this.connections.clear();
    this.hostWs = null;
  }

  // ---- Lobby ----

  private handleJoin(ws: WebSocket, msg: any): void {
    const { username } = msg;
    if (!username || typeof username !== "string") {
      this.send(ws, { type: "error", message: "Username required" });
      return;
    }

    if (!this.initialized) {
      this.send(ws, { type: "error", message: "Room not initialized" });
      return;
    }

    if (this.gameStarted) {
      // Allow reconnection if the username matches an existing player
      const existing = this.players.find(p => p.username === username && !p.isBot);
      if (existing) {
        const conn: PlayerConn = { ws, username, seat: existing.seat };
        this.connections.set(ws, conn);
        this.broadcastState();
        return;
      }
      this.send(ws, { type: "error", message: "Game already in progress" });
      return;
    }

    const humanCount = this.players.filter(p => !p.isBot).length;
    const maxHumans = this.numPlayers - this.numBots;

    if (humanCount >= maxHumans) {
      this.send(ws, { type: "error", message: "Room is full" });
      return;
    }

    // Assign next available seat
    const takenSeats = new Set(this.players.map(p => p.seat));
    let seat = 0;
    while (takenSeats.has(seat)) seat++;

    const slot: PlayerSlot = {
      username,
      seat,
      isBot: false,
      ready: true,
    };
    this.players.push(slot);

    const conn: PlayerConn = { ws, username, seat };
    this.connections.set(ws, conn);

    // Restore host by username, or assign first human as host
    if (this.hostUsername === username) {
      this.hostWs = ws;
    } else if (this.hostWs === null) {
      this.hostWs = ws;
      this.hostUsername = username;
    }

    this.broadcastLobby();
  }

  private broadcastLobby(): void {
    const playerList = this.players.map(p => ({
      username: p.username,
      seat: p.seat,
      isBot: p.isBot,
      ready: p.ready,
    }));

    for (const [ws] of this.connections) {
      this.send(ws, {
        type: "lobby",
        code: this.code,
        players: playerList,
        houseRules: this.houseRules,
        isHost: ws === this.hostWs,
      });
    }
  }

  // ---- Game Start ----

  private async handleStart(ws: WebSocket): Promise<void> {
    if (ws !== this.hostWs) {
      this.send(ws, { type: "error", message: "Only the host can start the game" });
      return;
    }

    if (this.gameStarted) {
      this.send(ws, { type: "error", message: "Game already started" });
      return;
    }

    const humanCount = this.players.filter(p => !p.isBot).length;
    if (humanCount === 0) {
      this.send(ws, { type: "error", message: "Need at least one human player" });
      return;
    }

    try {
      // Add bots to fill remaining seats
      const takenSeats = new Set(this.players.map(p => p.seat));
      for (let i = 0; i < this.numBots; i++) {
        let seat = 0;
        while (takenSeats.has(seat)) seat++;
        takenSeats.add(seat);

        this.players.push({
          username: pickBotName(),
          seat,
          isBot: true,
          botDifficulty: this.botDifficulty,
          ready: true,
        });
      }

      // If we still don't have enough players, add more bots
      while (this.players.length < this.numPlayers) {
        let seat = 0;
        while (takenSeats.has(seat)) seat++;
        takenSeats.add(seat);

        this.players.push({
          username: pickBotName(),
          seat,
          isBot: true,
          botDifficulty: this.botDifficulty,
          ready: true,
        });
      }

      // Sort players by seat
      this.players.sort((a, b) => a.seat - b.seat);

      // Initialize WASM
      await this.initWasm();

      this.gameStarted = true;

      // Resolve initial chance nodes (deal phase)
      this.resolveChanceNodes();

      // Run bot turns if it's a bot's turn first
      this.runBotTurns();

      this.broadcastState();
    } catch (e: any) {
      console.error("[GameRoom] Start error:", e.message, e.stack);
      this.send(ws, { type: "error", message: "Failed to start game: " + e.message });
    }
  }

  private async initWasm(): Promise<void> {
    this.wasm = await CoupWasm.create(this.numPlayers);
    // Default is ON (official rules). Only disable if explicitly set to false.
    if (this.houseRules.refundOnChallenge === false) {
      this.wasm.setRefundOnChallenge(false);
    }
  }

  // ---- Game Actions ----

  private async handleAction(ws: WebSocket, msg: any): Promise<void> {
    const conn = this.connections.get(ws);
    if (!conn) {
      this.send(ws, { type: "error", message: "Not connected" });
      return;
    }

    if (!this.gameStarted || !this.wasm) {
      this.send(ws, { type: "error", message: "Game not started" });
      return;
    }

    if (this.wasm.isDone()) {
      this.send(ws, { type: "error", message: "Game is over" });
      return;
    }

    const action = Number(msg.action);
    if (isNaN(action) || action < 0 || action >= NUM_ACTIONS) {
      this.send(ws, { type: "error", message: "Invalid action" });
      return;
    }

    // Verify it's this player's turn
    const activePlayer = this.wasm.getActivePlayer();
    if (conn.seat !== activePlayer) {
      this.send(ws, { type: "error", message: "Not your turn" });
      return;
    }

    // Verify action is valid
    const validMask = this.wasm.getValidActions();
    if (!((validMask >>> action) & 1)) {
      this.send(ws, { type: "error", message: "Action not valid" });
      return;
    }

    try {
      this.applyAction(action);
      this.resolveChanceNodes();
      this.runBotTurns();
      this.broadcastState();

      if (this.wasm.isDone()) {
        this.broadcastGameOver();
      }
    } catch (e: any) {
      console.error("[GameRoom] action error:", e.message, e.stack);
      this.send(ws, { type: "error", message: "Action failed: " + e.message });
    }
  }

  private applyAction(action: number): void {
    if (!this.wasm) return;

    const activePlayer = this.wasm.getActivePlayer();
    const playerName = this.getPlayerName(activePlayer);

    // Log the action
    this.logAction(action, activePlayer, playerName);

    // Snapshot alive players before the action
    const aliveBefore = new Set<number>();
    for (const p of this.players) {
      if (this.wasm.playerIsAlive(p.seat)) aliveBefore.add(p.seat);
    }

    // Apply via step_with_rng (handles chance internally for simple cases)
    this.wasm.stepWithRng(action);

    // Detect newly eliminated players
    for (const seat of aliveBefore) {
      if (!this.wasm.playerIsAlive(seat)) {
        this.eliminationOrder.push({ seat, turn: this.turnCounter });
      }
    }

    // Track turn changes
    if (this.wasm.getPhase() === PHASE_MAIN_ACTION) {
      this.turnCounter++;
    }
  }

  private flushPasses(): void {
    if (this.pendingPasses.length === 0) return;
    const names = this.pendingPasses.splice(0);
    const text = names.length === 1
      ? `${names[0]} passes.`
      : `${names.join(", ")} pass.`;
    this.history.push({ text, turn: this.turnCounter });
  }

  private logAction(action: number, seat: number, name: string): void {
    const playerNames = this.getPlayerNames();

    // Collapse consecutive passes
    if (action === ACTION_PASS) {
      this.pendingPasses.push(name);
      return;
    }
    this.flushPasses();

    // Suppress individual exchange discards
    if (this.wasm && this.wasm.getPhase() === PHASE_EXCHANGE_DISCARD
        && action >= ACTION_DISCARD_SLOT0 && action <= ACTION_DISCARD_SLOT0 + 3) {
      return;
    }

    let text: string;

    if (action === ACTION_INCOME) {
      text = `${name} takes Income. +1 coin.`;
    } else if (action === ACTION_FOREIGN_AID) {
      text = `${name} takes Foreign Aid. +2 coins.`;
    } else if (action === ACTION_TAX) {
      text = `${name} claims Duke for Tax. +3 coins.`;
    } else if (action === ACTION_EXCHANGE) {
      text = `${name} claims Ambassador for Exchange.`;
    } else if (action >= ACTION_COUP_P0 && action <= ACTION_COUP_P0 + 5) {
      const t = action - ACTION_COUP_P0;
      text = `${name} Coups ${playerNames[t]}. -7 coins.`;
    } else if (action >= ACTION_STEAL_P0 && action <= ACTION_STEAL_P0 + 5) {
      const t = action - ACTION_STEAL_P0;
      text = `${name} claims Captain to Steal from ${playerNames[t]}.`;
    } else if (action >= ACTION_ASSASSINATE_P0 && action <= ACTION_ASSASSINATE_P0 + 5) {
      const t = action - ACTION_ASSASSINATE_P0;
      text = `${name} claims Assassin to Assassinate ${playerNames[t]}. -3 coins.`;
    } else if (action === ACTION_CHALLENGE) {
      text = `${name} challenges!`;
    } else if (action === ACTION_BLOCK_CONTESSA) {
      text = `${name} blocks with Contessa.`;
    } else if (action === ACTION_BLOCK_CAPTAIN) {
      text = `${name} blocks with Captain.`;
    } else if (action === ACTION_BLOCK_AMBASSADOR) {
      text = `${name} blocks with Ambassador.`;
    } else if (action === ACTION_BLOCK_DUKE) {
      text = `${name} blocks with Duke.`;
    } else if (action >= ACTION_DISCARD_SLOT0 && action <= ACTION_DISCARD_SLOT0 + 3) {
      text = `${name} loses a card.`;
    } else {
      text = `${name} performs action ${action}.`;
    }

    // Track claimed roles (persists across full game, unlike history which is sliced)
    const claimed = claimedRole(action);
    let claimName: string | null = claimed != null ? (CARD_NAMES[claimed] ?? null) : null;
    if (!claimName) {
      if (action === ACTION_BLOCK_CONTESSA) claimName = "Contessa";
      else if (action === ACTION_BLOCK_CAPTAIN) claimName = "Captain";
      else if (action === ACTION_BLOCK_AMBASSADOR) claimName = "Ambassador";
      else if (action === ACTION_BLOCK_DUKE) claimName = "Duke";
    }
    if (claimName) {
      if (!this.claimedRoles.has(seat)) this.claimedRoles.set(seat, new Set());
      this.claimedRoles.get(seat)!.add(claimName);
    }

    this.history.push({ text, turn: this.turnCounter });
  }

  private resolveChanceNodes(): void {
    if (!this.wasm) return;

    // Resolve all consecutive chance nodes
    let safety = 100;
    while (!this.wasm.isDone() && this.wasm.isChanceNode() && safety-- > 0) {
      this.wasm.resolveChance();
    }
  }

  private runBotTurns(): void {
    if (!this.wasm) return;

    let safety = 200; // prevent infinite loops
    while (!this.wasm.isDone() && safety-- > 0) {
      // Resolve any chance nodes first
      this.resolveChanceNodes();
      if (this.wasm.isDone()) break;

      const active = this.wasm.getActivePlayer();
      const player = this.players.find(p => p.seat === active);

      if (!player || !player.isBot) break; // Human's turn

      const action = chooseBotAction(
        player.botDifficulty || this.botDifficulty,
        active,
        this.wasm,
      );

      this.applyAction(action);
    }

    // Final chance resolution after all bot turns
    if (!this.wasm.isDone()) {
      this.resolveChanceNodes();
    }
  }

  // ---- State Broadcasting ----

  private broadcastState(): void {
    if (!this.wasm) return;
    this.flushPasses();

    for (const [ws, conn] of this.connections) {
      const stateMsg = this.buildStateForPlayer(conn.seat);
      this.send(ws, stateMsg);
    }
  }

  private buildStateForPlayer(seat: number): ServerMessage {
    const wasm = this.wasm!;
    const playerNames = this.getPlayerNames();
    const phase = wasm.getPhase();
    const activePlayer = wasm.getActivePlayer();
    const pendingAction = wasm.getPendingAction();

    // Build your own cards (only the requesting player sees their face-down cards)
    const yourCards: { type: number; alive: boolean }[] = [];
    yourCards.push({
      type: wasm.playerCard0Type(seat),
      alive: wasm.playerCard0Alive(seat),
    });
    yourCards.push({
      type: wasm.playerCard1Type(seat),
      alive: wasm.playerCard1Alive(seat),
    });

    // During exchange discard, if it's our turn, also show exchange cards
    if (phase === PHASE_EXCHANGE_DISCARD && activePlayer === seat) {
      yourCards.push({
        type: wasm.exchangeCard0(),
        alive: true,
      });
      yourCards.push({
        type: wasm.exchangeCard1(),
        alive: true,
      });
    }

    // Build player list
    const players = this.players.map(p => {
      const s = p.seat;
      const alive = wasm.playerIsAlive(s);
      const c0Alive = wasm.playerCard0Alive(s);
      const c1Alive = wasm.playerCard1Alive(s);
      const c0Type = wasm.playerCard0Type(s);
      const c1Type = wasm.playerCard1Type(s);

      // Influence = number of alive cards
      const influence = (c0Alive ? 1 : 0) + (c1Alive ? 1 : 0);

      // Revealed cards: dead cards are visible to all
      const revealed: { type: number }[] = [];
      if (!c0Alive) revealed.push({ type: c0Type });
      if (!c1Alive) revealed.push({ type: c1Type });

      return {
        name: p.username,
        coins: wasm.playerCoins(s),
        influence,
        revealed,
        isBot: p.isBot,
        alive,
      };
    });

    // Available actions for this player
    const availableActions: { id: number; label: string }[] = [];
    if (activePlayer === seat && !wasm.isDone()) {
      const mask = wasm.getValidActions();
      const valid = getValidActionsFromMask(mask);
      for (const a of valid) {
        availableActions.push({ id: a, label: actionLabel(a, playerNames) });
      }
    }

    // Build context string matching TUI grammar
    let context: string | undefined;
    const phaseName = PHASE_NAMES[phase] || "unknown";
    const isYou = activePlayer === seat;
    if (phase === PHASE_MAIN_ACTION) {
      const coins = wasm.playerCoins(activePlayer);
      if (isYou) {
        context = coins >= 10 ? "You must Coup (10+ coins)." : "Your turn. Choose an action:";
      } else {
        context = `${playerNames[activePlayer]}'s turn.`;
      }
    } else if (phase === PHASE_CHALLENGE_ACTION && pendingAction >= 0) {
      const turnPlayer = wasm.getTurnPlayer();
      const tpName = turnPlayer === seat ? "You" : playerNames[turnPlayer];
      const role = claimedRole(pendingAction);
      const roleName = role != null ? CARD_NAMES[role] : "?";
      const target = actionTarget(pendingAction);
      const targetName = target != null ? (target === seat ? "You" : playerNames[target]) : undefined;
      if (pendingAction >= ACTION_STEAL_P0 && pendingAction <= ACTION_STEAL_P0 + 5) {
        context = `${tpName} claims ${roleName} to Steal from ${targetName}. Challenge?`;
      } else if (pendingAction >= ACTION_ASSASSINATE_P0 && pendingAction <= ACTION_ASSASSINATE_P0 + 5) {
        context = `${tpName} claims ${roleName} to Assassinate ${targetName}. Challenge?`;
      } else if (pendingAction === ACTION_TAX) {
        context = `${tpName} claims ${roleName} for Tax. Challenge?`;
      } else if (pendingAction === ACTION_EXCHANGE) {
        context = `${tpName} claims ${roleName} for Exchange. Challenge?`;
      } else {
        context = `${tpName} made a claim. Challenge?`;
      }
    } else if (phase === PHASE_BLOCK && pendingAction >= 0) {
      const turnPlayer = wasm.getTurnPlayer();
      const tpName = turnPlayer === seat ? "You" : playerNames[turnPlayer];
      const target = actionTarget(pendingAction);
      const targetName = target != null ? (target === seat ? "You" : playerNames[target]) : undefined;
      if (pendingAction === ACTION_FOREIGN_AID) {
        context = `${tpName} attempts Foreign Aid. Block with Duke?`;
      } else if (pendingAction >= ACTION_STEAL_P0 && pendingAction <= ACTION_STEAL_P0 + 5) {
        context = `${tpName} Steals from ${targetName}. Block?`;
      } else if (pendingAction >= ACTION_ASSASSINATE_P0 && pendingAction <= ACTION_ASSASSINATE_P0 + 5) {
        context = `${tpName} Assassinates ${targetName}. Block?`;
      } else {
        context = `${tpName} acted. Block?`;
      }
    } else if (phase === PHASE_CHALLENGE_BLOCK) {
      context = "A block was declared. Challenge the block?";
    } else if (phase === PHASE_LOSE_CARD) {
      context = isYou ? "You must lose an influence. Choose a card:" : `${playerNames[activePlayer]} must lose an influence.`;
    } else if (phase === PHASE_EXCHANGE_DISCARD) {
      context = isYou ? "Ambassador Exchange. Choose cards to keep:" : `${playerNames[activePlayer]} is exchanging cards.`;
    } else if (pendingAction >= 0) {
      const turnPlayer = wasm.getTurnPlayer();
      context = `${playerNames[turnPlayer]} played ${actionLabel(pendingAction, playerNames)}`;
    }

    return {
      type: "state",
      yourSeat: seat,
      yourCards,
      players,
      phase: phaseName,
      activePlayer,
      isYourTurn: activePlayer === seat,
      availableActions,
      history: this.history.slice(-50), // last 50 entries
      claims: Object.fromEntries(
        Array.from(this.claimedRoles.entries()).map(([s, roles]) => [s, Array.from(roles)])
      ),
      context,
      pendingAction: pendingAction >= 0 ? pendingAction : undefined,
      deckSize: wasm.deckTotal(),
    };
  }

  private broadcastGameOver(): void {
    if (!this.wasm) return;
    this.flushPasses();

    const winner = this.wasm.getWinner();
    const winnerPlayer = this.players.find(p => p.seat === winner);
    const winnerName = winnerPlayer?.username || `Player ${winner}`;

    // Build placements: winner first, then reverse elimination order
    const finalStandings: { seat: number; name: string; alive: boolean; eliminatedTurn?: number }[] = [];
    finalStandings.push({ seat: winner, name: winnerName, alive: true });
    for (let i = this.eliminationOrder.length - 1; i >= 0; i--) {
      const e = this.eliminationOrder[i];
      const player = this.players.find(p => p.seat === e.seat);
      finalStandings.push({
        seat: e.seat,
        name: player?.username || `Player ${e.seat}`,
        alive: false,
        eliminatedTurn: e.turn,
      });
    }

    const gameOverMsg: ServerMessage = {
      type: "game_over",
      winner,
      winnerName,
      finalStandings,
      totalTurns: this.turnCounter,
      history: this.history,
    };

    for (const [ws] of this.connections) {
      this.send(ws, gameOverMsg);
    }

    // Clean up WASM memory
    this.wasm.dispose();
  }

  // ---- Helpers ----

  private getPlayerNames(): string[] {
    const names: string[] = [];
    for (let i = 0; i < 6; i++) {
      const p = this.players.find(pl => pl.seat === i);
      names.push(p?.username || `Player ${i}`);
    }
    return names;
  }

  private getPlayerName(seat: number): string {
    const p = this.players.find(pl => pl.seat === seat);
    return p?.username || `Player ${seat}`;
  }

  private send(ws: WebSocket, msg: object): void {
    try {
      ws.send(JSON.stringify(msg));
    } catch {
      // WebSocket already closed
    }
  }
}
