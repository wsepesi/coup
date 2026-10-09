// GameRoom Durable Object — one per room code.
//
// - WebSocket Hibernation API: the DO can be evicted between messages without
//   dropping sockets; per-socket identity lives in the socket attachment.
// - All room state (roster, rules, engine bytes, history) is persisted to DO
//   storage after every change, so eviction, deploys and crashes don't lose games.
// - Bots, response timers, away-player takeover, lobby cleanup and room expiry
//   are all driven by a single alarm, so bot moves are paced and visible.
// - Seats are bound to a client-generated secret id (cid) that is never sent to
//   other clients; knowing someone's display name is not enough to take a seat.

import { CoupGame, loadEngine } from "./engine.js";
import { chooseBotAction, pickBotName, type BotView } from "./bot.js";
import {
  type Env,
  type BotDifficulty,
  type HouseRules,
  type HistoryEntry,
  type HistoryKind,
  type GameResult,
  type GameView,
  type LobbySeat,
  type PlayerView,
  type ServerMessage,
  PROTOCOL,
  DEFAULT_RULES,
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
  isCoup,
  isSteal,
  isAssassinate,
  isBlock,
  isDifficulty,
  actionLabel,
  actionTarget,
  claimedRole,
  blockRole,
} from "./types.js";

export const MAX_SEATS = 6;
const NAME_MAX = 20;
const HISTORY_TAIL = 40;
// Whole-game history (perfect recall). A 6-player MAX_TURNS game logs well under this.
const HISTORY_CAP = 12000;
const RESPONSE_TIMEOUT_MS = 20_000; // challenge/block windows for connected humans
const AWAY_GRACE_MS = 30_000; // disconnected human is auto-played after this
const LOBBY_DROP_MS = 60_000; // disconnected human is removed from the lobby after this
const EMPTY_ROOM_TTL_MS = 30 * 60_000; // storage wiped once nobody has been connected this long
const FORCED_MOVE_DELAY_MS = 800;

/** Close codes understood by the client (4000-4999 are application codes). */
export const CLOSE_LEFT = 4000;
export const CLOSE_REPLACED = 4001;
export const CLOSE_KICKED = 4003;
export const CLOSE_NOT_FOUND = 4004;

interface Seat {
  name: string;
  /** Secret client id of the human in this seat; null for bots. */
  cid: string | null;
  bot: BotDifficulty | null;
}

interface GameSeat extends Seat {
  /** Human who forfeited; a bot plays the seat out. */
  left?: boolean;
}

interface GameData {
  bytes: Uint8Array;
  seats: GameSeat[];
  history: HistoryEntry[];
  claims: number[][];
  /** Roles each seat currently claims to hold: claimed, and not since shown, caught bluffing, lost face up, or exchanged away. */
  held?: number[][];
  /** Cards seen going into the deck since anyone last drew from it (private: only `seat` saw them). */
  shuffledIn?: { cards: number[]; seat: number; private: boolean; turn: number; via: "reveal" | "exchange" } | null;
  /** Alive hand + drawn cards while an exchange is being resolved. */
  exchangePool?: number[] | null;
  eliminated: { seat: number; turn: number }[];
  turn: number;
  /** Incremented on every applied action — identifies the current decision. */
  step: number;
  decisionSince: number;
  turnAction: number;
  turnActor: number;
  turnCoins: number[];
  turnBlock: { seat: number; card: number } | null;
  turnBlockFailed: boolean;
  /** When the server will act for the current decision (bot / timeout / away). */
  due: number | null;
}

interface RoomData {
  code: string;
  createdAt: number;
  hostCid: string | null;
  rules: HouseRules;
  seats: Seat[];
  autoStart: boolean;
  /** cid -> time their last socket closed (absent while connected). */
  offlineSince: Record<string, number>;
  /** Last time any socket was connected (for expiry). */
  lastSeen: number;
  game: GameData | null;
}

interface Attachment {
  cid: string | null;
  name: string | null;
}

export interface InitBody {
  code: string;
  hostCid: string;
  rules?: Partial<HouseRules>;
  bots?: BotDifficulty[];
  autoStart?: boolean;
}

const CID_RE = /^[A-Za-z0-9_-]{16,64}$/;

export function sanitizeName(raw: unknown): string {
  if (typeof raw !== "string") return "";
  // Strip control, zero-width and bidi-override characters (name spoofing).
  const bad = (c: number) => c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f) || (c >= 0x2028 && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069);
  return Array.from(raw).filter((ch) => !bad(ch.codePointAt(0)!)).join("").replace(/\s+/g, " ").trim().slice(0, NAME_MAX);
}

function sanitizeRules(r: unknown, base: HouseRules): HouseRules {
  const o = (r && typeof r === "object" ? r : {}) as Partial<HouseRules>;
  return {
    refundOnChallenge: typeof o.refundOnChallenge === "boolean" ? o.refundOnChallenge : base.refundOnChallenge,
    responseTimer: typeof o.responseTimer === "boolean" ? o.responseTimer : base.responseTimer,
  };
}

function uniqueName(want: string, taken: string[]): string {
  const used = new Set(taken.map((n) => n.toLowerCase()));
  if (!used.has(want.toLowerCase())) return want;
  for (let i = 2; ; i++) {
    const n = `${want.slice(0, NAME_MAX - 3)} ${i}`;
    if (!used.has(n.toLowerCase())) return n;
  }
}

function shuffle<T>(xs: T[]): T[] {
  const a = [...xs];
  const r = new Uint32Array(a.length);
  crypto.getRandomValues(r);
  for (let i = a.length - 1; i > 0; i--) {
    const j = r[i] % (i + 1);
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

export class GameRoom implements DurableObject {
  private ctx: DurableObjectState;
  private room: RoomData | null = null;
  private game: CoupGame | null = null;
  /** Sockets whose close we're handling; getWebSockets() may still list them. */
  private closing = new WeakSet<WebSocket>();

  constructor(ctx: DurableObjectState, _env: Env) {
    this.ctx = ctx;
    // Answer heartbeats without waking the object.
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"type":"ping"}', '{"type":"pong"}'));
    ctx.blockConcurrencyWhile(async () => {
      await loadEngine();
      this.room = (await ctx.storage.get<RoomData>("room")) ?? null;
      if (this.room?.game) {
        try {
          this.game = CoupGame.restore(this.room.game.bytes);
        } catch (e) {
          // Engine layout changed under a running game (deploy mid-game): drop the game, keep the lobby.
          console.error("[GameRoom] could not restore game:", (e as Error).message);
          this.room.game = null;
          this.save();
        }
      }
    });
  }

  // ---------------------------------------------------------------------------
  // HTTP entry points

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/init" && request.method === "POST") {
      if (this.room) return new Response("exists", { status: 409 });
      const body = (await request.json()) as InitBody;
      const now = Date.now();
      const seats: Seat[] = [];
      for (const d of (body.bots ?? []).slice(0, MAX_SEATS - 1)) {
        seats.push({ name: pickBotName(seats.map((s) => s.name)), cid: null, bot: isDifficulty(d) ? d : "medium" });
      }
      this.room = {
        code: body.code,
        createdAt: now,
        hostCid: body.hostCid,
        rules: sanitizeRules(body.rules, DEFAULT_RULES),
        seats,
        autoStart: !!body.autoStart,
        offlineSince: {},
        lastSeen: now,
        game: null,
      };
      this.save();
      await this.scheduleAlarm();
      return new Response("ok");
    }

    if (url.pathname === "/ws") {
      if (request.headers.get("Upgrade") !== "websocket") {
        return new Response("Expected WebSocket", { status: 426 });
      }
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.ctx.acceptWebSocket(server);
      server.serializeAttachment({ cid: null, name: null } satisfies Attachment);
      if (!this.room) {
        this.send(server, { type: "error", message: "Room not found. It may have expired.", fatal: true });
        server.close(CLOSE_NOT_FOUND, "Room not found");
      } else {
        this.send(server, { type: "welcome", protocol: PROTOCOL, code: this.room.code });
      }
      return new Response(null, { status: 101, webSocket: client });
    }

    return new Response("Not found", { status: 404 });
  }

  // ---------------------------------------------------------------------------
  // WebSocket handlers (hibernation API)

  async webSocketMessage(ws: WebSocket, data: string | ArrayBuffer): Promise<void> {
    if (typeof data !== "string" || data.length > 4096) return;
    let msg: any;
    try {
      msg = JSON.parse(data);
    } catch {
      return this.send(ws, { type: "error", message: "Invalid message" });
    }
    if (!this.room || !msg || typeof msg !== "object") return;

    try {
      switch (msg.type) {
        case "join": this.onJoin(ws, msg); break;
        case "leave": this.onLeave(ws); break;
        case "start": this.onStart(ws); break;
        case "add_bot": this.onAddBot(ws, msg); break;
        case "remove_seat": this.onRemoveSeat(ws, msg); break;
        case "set_bot": this.onSetBot(ws, msg); break;
        case "rules": this.onRules(ws, msg); break;
        case "action": this.onAction(ws, msg); break;
        case "ping": this.send(ws, { type: "pong" }); return;
        default: this.send(ws, { type: "error", message: "Unknown message type" }); return;
      }
    } catch (e) {
      console.error("[GameRoom] handler error:", (e as Error).message, (e as Error).stack);
      this.send(ws, { type: "error", message: "Server error — please retry." });
    }
    await this.scheduleAlarm();
  }

  async webSocketClose(ws: WebSocket, code: number): Promise<void> {
    try { ws.close(code === 1005 || code === 1006 ? 1000 : code, "closed"); } catch { /* already closed */ }
    await this.onSocketGone(ws);
  }

  async webSocketError(ws: WebSocket): Promise<void> {
    await this.onSocketGone(ws);
  }

  private async onSocketGone(ws: WebSocket): Promise<void> {
    this.closing.add(ws);
    if (!this.room) return;
    const { cid } = this.attachment(ws);
    if (cid && !this.socketsFor(cid, ws).length && this.isSeated(cid)) {
      this.room.offlineSince[cid] = Date.now();
      this.room.lastSeen = Date.now();
      this.save();
      this.broadcast();
    }
    await this.scheduleAlarm();
  }

  // ---------------------------------------------------------------------------
  // Lobby

  private onJoin(ws: WebSocket, msg: any): void {
    const room = this.room!;
    const cid = typeof msg.cid === "string" && CID_RE.test(msg.cid) ? msg.cid : null;
    if (!cid) return this.send(ws, { type: "error", message: "Invalid client id", fatal: true });
    const name = sanitizeName(msg.name) || "Player";

    // One live socket per client id: a newer tab takes over.
    for (const other of this.socketsFor(cid, ws)) {
      this.send(other, { type: "error", message: "This game was opened in another tab.", fatal: true });
      try { other.close(CLOSE_REPLACED, "Replaced by another tab"); } catch { /* closed */ }
    }
    ws.serializeAttachment({ cid, name } satisfies Attachment);
    delete room.offlineSince[cid];
    room.lastSeen = Date.now();

    if (room.game) {
      // Reconnect (or spectate). Seat binding is by secret cid, never by name.
      this.save();
      this.sendState(ws, true);
      this.broadcast(ws);
      return;
    }

    let seat = room.seats.find((s) => s.cid === cid);
    if (seat) {
      if (seat.name !== name) seat.name = uniqueName(name, room.seats.filter((s) => s !== seat).map((s) => s.name));
    } else if (room.seats.length >= MAX_SEATS) {
      this.send(ws, { type: "error", message: "This room is full — you're watching." });
    } else {
      seat = { name: uniqueName(name, room.seats.map((s) => s.name)), cid, bot: null };
      // The creator always sits first; everyone else joins in arrival order.
      if (cid === room.hostCid) room.seats.unshift(seat);
      else room.seats.push(seat);
    }
    this.ensureHost();
    this.save();

    if (room.autoStart && cid === room.hostCid && room.seats.length >= 2) {
      room.autoStart = false;
      this.startGame();
      return;
    }
    this.broadcast();
  }

  private onLeave(ws: WebSocket): void {
    const room = this.room!;
    const { cid } = this.attachment(ws);
    if (!cid) return;
    if (room.game) {
      const gd = room.game;
      const idx = gd.seats.findIndex((s) => s.cid === cid);
      if (idx >= 0 && !gd.seats[idx].left) {
        gd.seats[idx].left = true;
        if (this.game!.isAlive(idx)) this.log(`${gd.seats[idx].name} left the game — a bot plays their cards.`, "info", idx);
        const humansRemaining = gd.seats.some((s) => s.cid && !s.left);
        if (!humansRemaining) {
          this.abortGame();
        } else {
          this.rescheduleDecision();
        }
      }
    } else {
      room.seats = room.seats.filter((s) => s.cid !== cid);
      this.ensureHost();
    }
    ws.serializeAttachment({ cid: null, name: null } satisfies Attachment);
    this.save();
    this.broadcast();
    try { ws.close(CLOSE_LEFT, "Left"); } catch { /* closed */ }
  }

  private onAddBot(ws: WebSocket, msg: any): void {
    const room = this.room!;
    if (!this.requireHostInLobby(ws)) return;
    if (room.seats.length >= MAX_SEATS) return this.send(ws, { type: "error", message: "Room is full" });
    const bot: BotDifficulty = isDifficulty(msg.difficulty) ? msg.difficulty : "medium";
    room.seats.push({ name: pickBotName(room.seats.map((s) => s.name)), cid: null, bot });
    this.save();
    this.broadcast();
  }

  private onRemoveSeat(ws: WebSocket, msg: any): void {
    const room = this.room!;
    if (!this.requireHostInLobby(ws)) return;
    const i = Number(msg.index);
    const seat = room.seats[i];
    if (!Number.isInteger(i) || !seat || seat.cid === room.hostCid) return;
    room.seats.splice(i, 1);
    if (seat.cid) {
      for (const s of this.socketsFor(seat.cid)) {
        this.send(s, { type: "error", message: "The host removed you from the room.", fatal: true });
        s.serializeAttachment({ cid: null, name: null } satisfies Attachment);
        try { s.close(CLOSE_KICKED, "Removed by host"); } catch { /* closed */ }
      }
      delete room.offlineSince[seat.cid];
    }
    this.save();
    this.broadcast();
  }

  private onSetBot(ws: WebSocket, msg: any): void {
    const room = this.room!;
    if (!this.requireHostInLobby(ws)) return;
    const seat = room.seats[Number(msg.index)];
    if (!seat || !seat.bot || !isDifficulty(msg.difficulty)) return;
    seat.bot = msg.difficulty;
    this.save();
    this.broadcast();
  }

  private onRules(ws: WebSocket, msg: any): void {
    const room = this.room!;
    if (!this.requireHostInLobby(ws)) return;
    room.rules = sanitizeRules(msg.rules, room.rules);
    this.save();
    this.broadcast();
  }

  private onStart(ws: WebSocket): void {
    const room = this.room!;
    if (!this.requireHostInLobby(ws)) return;
    if (room.seats.length < 2) return this.send(ws, { type: "error", message: "Add a bot or invite a friend first — Coup needs at least 2 players." });
    this.startGame();
  }

  private requireHostInLobby(ws: WebSocket): boolean {
    const room = this.room!;
    const { cid } = this.attachment(ws);
    if (!cid || cid !== room.hostCid) {
      this.send(ws, { type: "error", message: "Only the host can do that." });
      return false;
    }
    if (room.game) {
      this.send(ws, { type: "error", message: "The game has already started." });
      return false;
    }
    return true;
  }

  /** Host is the creator while seated; otherwise the first seated human (preferring online ones). */
  private ensureHost(): void {
    const room = this.room!;
    const humans = room.seats.filter((s) => s.cid);
    if (room.hostCid && humans.some((s) => s.cid === room.hostCid)) return;
    const online = humans.find((s) => this.socketsFor(s.cid!).length > 0);
    room.hostCid = (online ?? humans[0])?.cid ?? room.hostCid;
  }

  // ---------------------------------------------------------------------------
  // Game lifecycle

  private startGame(): void {
    const room = this.room!;
    const seats: GameSeat[] = shuffle(room.seats).map((s) => ({ ...s }));
    this.game = CoupGame.create(seats.length, room.rules.refundOnChallenge);
    const now = Date.now();
    room.game = {
      bytes: this.game.bytes,
      seats,
      history: [],
      claims: seats.map(() => []),
      held: seats.map(() => []),
      shuffledIn: null,
      exchangePool: null,
      eliminated: [],
      turn: 0,
      step: 0,
      decisionSince: now,
      turnAction: -1,
      turnActor: -1,
      turnCoins: seats.map((_, i) => this.game!.coins(i)),
      turnBlock: null,
      turnBlockFailed: false,
      due: null,
    };
    this.log(`New game — ${seats.length} players. ${seats[this.game.turnPlayer()].name} goes first.`, "info");
    this.rescheduleDecision();
    this.save();
    this.broadcast();
  }

  private abortGame(): void {
    const room = this.room!;
    if (room.game) {
      const leftCids = new Set(room.game.seats.filter((s) => s.left && s.cid).map((s) => s.cid!));
      room.seats = room.seats.filter((s) => !s.cid || !leftCids.has(s.cid));
    }
    room.game = null;
    this.game = null;
    this.ensureHost();
  }

  private finishGame(): void {
    const room = this.room!;
    const gd = room.game!;
    const g = this.game!;
    const winner = g.winner();
    const names = gd.seats.map((s) => s.name);
    this.log(`${names[winner] ?? "Nobody"} wins!`, "win", winner);

    const standings: GameResult["standings"] = [];
    // Survivors (normally just the winner; more only on the turn-limit tiebreak).
    const survivors = gd.seats.map((_, i) => i).filter((i) => g.isAlive(i))
      .sort((a, b) => (a === winner ? -1 : b === winner ? 1 : g.influence(b) - g.influence(a) || g.coins(b) - g.coins(a)));
    for (const i of survivors) standings.push({ seat: i, name: names[i], bot: !!gd.seats[i].bot, alive: true });
    for (let k = gd.eliminated.length - 1; k >= 0; k--) {
      const e = gd.eliminated[k];
      standings.push({ seat: e.seat, name: names[e.seat], bot: !!gd.seats[e.seat].bot, alive: false, eliminatedTurn: e.turn });
    }
    const finalCards = gd.seats.map((_, i) => ([0, 1] as const).filter((s) => g.cardAlive(i, s)).map((s) => g.cardType(i, s)));
    const result: GameResult = { winner, winnerName: names[winner] ?? "Nobody", standings, turns: gd.turn, history: gd.history, finalCards };

    // Final board (with the winning blow) so clients can show it before the results screen.
    this.broadcast();
    this.abortGame();
    // Anyone who was watching joins the next game if there's room.
    for (const ws of this.sockets()) {
      const a = this.attachment(ws);
      if (a.cid && a.name && !room.seats.some((s) => s.cid === a.cid) && room.seats.length < MAX_SEATS) {
        room.seats.push({ name: uniqueName(a.name, room.seats.map((s) => s.name)), cid: a.cid, bot: null });
      }
    }
    this.ensureHost();
    this.save();
    for (const ws of this.sockets()) this.send(ws, { type: "game_over", ...result });
    this.broadcast();
  }

  // ---------------------------------------------------------------------------
  // Actions

  private onAction(ws: WebSocket, msg: any): void {
    const room = this.room!;
    const gd = room.game;
    const g = this.game;
    if (!gd || !g || g.isDone()) return this.send(ws, { type: "error", message: "No game in progress." });
    const { cid } = this.attachment(ws);
    const seat = cid ? gd.seats.findIndex((s) => s.cid === cid && !s.left) : -1;
    if (seat < 0) return this.send(ws, { type: "error", message: "You're not playing in this game." });

    // Stale clicks (double-submit, timer raced the click) are answered with fresh state, not an error.
    const step = Number(msg.step);
    if ((Number.isInteger(step) && step !== gd.step) || g.activePlayer() !== seat) return this.sendState(ws);
    const action = Number(msg.action);
    if (!g.isValid(action)) return this.send(ws, { type: "error", message: "That move isn't allowed right now." });

    const exchanging = g.phase() === PHASE_EXCHANGE_DISCARD;
    this.apply(action);
    // Exchange returns two cards as two engine steps; the client sends both at once.
    if (exchanging && msg.then != null && !g.isDone() && g.phase() === PHASE_EXCHANGE_DISCARD && g.activePlayer() === seat) {
      const then = Number(msg.then);
      if (g.isValid(then)) this.apply(then);
    }
    this.afterChange();
  }

  /** Persist, schedule the next server-driven move, and broadcast. */
  private afterChange(): void {
    if (this.game?.isDone()) return this.finishGame();
    this.rescheduleDecision();
    this.save();
    this.broadcast();
  }

  private log(text: string, kind: HistoryKind, seat?: number): void {
    const gd = this.room!.game!;
    gd.history.push(seat == null ? { turn: gd.turn, text, kind } : { turn: gd.turn, text, kind, seat });
    if (gd.history.length > HISTORY_CAP) gd.history.splice(0, gd.history.length - HISTORY_CAP);
  }

  /** Apply one engine action for the active player and narrate what happened. */
  private apply(action: number): void {
    const g = this.game!;
    const gd = this.room!.game!;
    if (!g.isValid(action)) throw new Error(`invalid action ${action}`); // before narrating anything
    const n = gd.seats.length;
    const N = (s: number) => gd.seats[s]?.name ?? `Player ${s + 1}`;
    const phase = g.phase();
    const actor = g.activePlayer();
    const tp = g.turnPlayer();
    const pending = g.pendingAction();
    const blocker = g.blocker();
    const blockCard = g.blockCard();
    const aliveBefore = Array.from({ length: n }, (_, i) => [g.cardAlive(i, 0), g.cardAlive(i, 1)]);
    const coinsBefore = Array.from({ length: n }, (_, i) => g.coins(i));
    const held = (gd.held ??= gd.seats.map(() => []));
    const claim = (seat: number, role: number | null) => {
      if (role == null) return;
      if (!gd.claims[seat].includes(role)) gd.claims[seat].push(role);
      if (!held[seat].includes(role)) held[seat].push(role);
    };
    const unclaim = (seat: number, role: number) => {
      held[seat] = held[seat].filter((r) => r !== role);
    };
    const handOf = (seat: number) => ([0, 1] as const).filter((s) => g.cardAlive(seat, s)).map((s) => g.cardType(seat, s));

    if (phase === PHASE_MAIN_ACTION) {
      gd.turn++;
      gd.turnAction = action;
      gd.turnActor = actor;
      gd.turnCoins = coinsBefore;
      gd.turnBlock = null;
      gd.turnBlockFailed = false;
      const t = actionTarget(action);
      if (action === ACTION_INCOME) this.log(`${N(actor)} takes Income (+1).`, "action", actor);
      else if (action === ACTION_FOREIGN_AID) this.log(`${N(actor)} takes Foreign Aid.`, "action", actor);
      else if (action === ACTION_TAX) this.log(`${N(actor)} claims Duke to take Tax.`, "claim", actor);
      else if (action === ACTION_EXCHANGE) this.log(`${N(actor)} claims Ambassador to Exchange.`, "claim", actor);
      else if (isCoup(action)) this.log(`${N(actor)} pays 7 to Coup ${N(t!)}.`, "action", actor);
      else if (isSteal(action)) this.log(`${N(actor)} claims Captain to steal from ${N(t!)}.`, "claim", actor);
      else if (isAssassinate(action)) this.log(`${N(actor)} pays 3 and claims Assassin to assassinate ${N(t!)}.`, "claim", actor);
      claim(actor, claimedRole(action));
    } else if (action === ACTION_CHALLENGE && phase === PHASE_CHALLENGE_ACTION) {
      this.log(`${N(actor)} challenges ${N(tp)}'s ${CARD_NAMES[claimedRole(pending) ?? 0]}!`, "challenge", actor);
    } else if (action === ACTION_CHALLENGE && phase === PHASE_CHALLENGE_BLOCK) {
      this.log(`${N(actor)} challenges ${N(blocker)}'s ${CARD_NAMES[blockCard]} block!`, "challenge", actor);
    } else if (isBlock(action)) {
      const role = blockRole(action)!;
      this.log(`${N(actor)} blocks, claiming ${CARD_NAMES[role]}.`, "block", actor);
      claim(actor, role);
      gd.turnBlock = { seat: actor, card: role };
    } else if (action === ACTION_PASS) {
      this.log(`${N(actor)} passes.`, "pass", actor);
    }

    if (!g.step(action)) {
      // Callers validate first; the engine refusing means our view of the state is wrong.
      throw new Error(`engine rejected action ${action} in phase ${phase}`);
    }
    gd.step++;
    gd.decisionSince = Date.now();
    gd.due = null;

    if (action === ACTION_CHALLENGE) {
      const claimant = phase === PHASE_CHALLENGE_ACTION ? tp : blocker;
      const role = phase === PHASE_CHALLENGE_ACTION ? claimedRole(pending) ?? 0 : blockCard;
      const challengerLoses = g.phase() === PHASE_LOSE_CARD && g.activePlayer() === actor;
      unclaim(claimant, role);
      if (challengerLoses) {
        // Shown, shuffled back, and a replacement drawn: the deck now holds a card everyone saw.
        gd.shuffledIn = { cards: [role], seat: claimant, private: false, turn: gd.turn, via: "reveal" };
        this.log(`${N(claimant)} reveals a ${CARD_NAMES[role]} — the challenge fails. It's shuffled back and replaced.`, "reveal", claimant);
      } else {
        const refund = phase === PHASE_CHALLENGE_ACTION && isAssassinate(pending) && this.room!.rules.refundOnChallenge;
        this.log(`${N(claimant)} was bluffing — no ${CARD_NAMES[role]}!${refund ? " The 3 coins are returned." : ""}`, "reveal", claimant);
        if (phase === PHASE_CHALLENGE_BLOCK) gd.turnBlockFailed = true;
      }
    }

    for (let i = 0; i < n; i++) {
      for (const s of [0, 1] as const) {
        if (aliveBefore[i][s] && !g.cardAlive(i, s)) {
          this.log(`${N(i)} loses ${CARD_NAMES[g.cardType(i, s)]}.`, "lose", i);
          unclaim(i, g.cardType(i, s));
        }
      }
      if ((aliveBefore[i][0] || aliveBefore[i][1]) && !g.isAlive(i)) {
        held[i] = [];
        gd.eliminated.push({ seat: i, turn: gd.turn });
        this.log(`${N(i)} is out of the game.`, "elim", i);
      }
    }

    if (phase !== PHASE_EXCHANGE_DISCARD && g.phase() === PHASE_EXCHANGE_DISCARD) {
      // Two cards drawn: whatever was known to be in the deck may be in a hand now.
      const ex = g.activePlayer();
      gd.shuffledIn = null;
      gd.exchangePool = [...handOf(ex), ...g.exchangeCards()];
    }
    if (phase === PHASE_EXCHANGE_DISCARD && g.phase() !== PHASE_EXCHANGE_DISCARD) {
      this.log(`${N(actor)} exchanged cards with the deck.`, "action", actor);
      held[actor] = [];
      const returned = [...(gd.exchangePool ?? [])];
      for (const c of handOf(actor)) {
        const k = returned.indexOf(c);
        if (k >= 0) returned.splice(k, 1);
      }
      gd.shuffledIn = gd.exchangePool ? { cards: returned, seat: actor, private: true, turn: gd.turn, via: "exchange" } : null;
      gd.exchangePool = null;
    }

    // Turn resolved: summarize the outcome of the main action.
    const turnOver = g.phase() === PHASE_MAIN_ACTION || g.isDone();
    if (turnOver && phase !== PHASE_MAIN_ACTION) this.summarizeTurn(N);
  }

  private summarizeTurn(N: (s: number) => string): void {
    const g = this.game!;
    const gd = this.room!.game!;
    const a = gd.turnAction;
    const actor = gd.turnActor;
    if (actor < 0) return;
    const gained = g.coins(actor) - gd.turnCoins[actor];
    if (gd.turnBlock && !gd.turnBlockFailed) {
      const what = a === ACTION_FOREIGN_AID ? "Foreign Aid" : isSteal(a) ? "The steal" : isAssassinate(a) ? "The assassination" : "The action";
      this.log(`${what} is blocked by ${N(gd.turnBlock.seat)}.`, "block", actor);
      return;
    }
    if ((a === ACTION_FOREIGN_AID || a === ACTION_TAX) && gained > 0) this.log(`${N(actor)} collects ${gained} coins.`, "action", actor);
    else if (isSteal(a) && gained > 0) this.log(`${N(actor)} steals ${gained} from ${N(actionTarget(a)!)}.`, "action", actor);
    else if (isSteal(a) && gd.turnCoins[actionTarget(a)!] === 0 && g.isAlive(actor)) this.log(`${N(actionTarget(a)!)} had nothing to steal.`, "info", actor);
  }

  // ---------------------------------------------------------------------------
  // Server-driven moves: bots, timers, away players

  /** Whether a seated human has been disconnected long enough to be auto-played. */
  private isAway(s: GameSeat): boolean {
    if (!s.cid) return false;
    if (s.left) return true;
    const off = this.room!.offlineSince[s.cid];
    return off != null && this.socketsFor(s.cid).length === 0 && Date.now() - off >= AWAY_GRACE_MS;
  }

  private botDelay(phase: number, step: number): number {
    const jitter = (step * 7919) % 400;
    if (phase === PHASE_MAIN_ACTION) return 1100 + jitter;
    if (phase === PHASE_LOSE_CARD) return 900 + jitter / 2;
    if (phase === PHASE_EXCHANGE_DISCARD) return 1000;
    return 450 + jitter / 2; // challenge/block windows
  }

  /**
   * When (if ever) the server will act for the current decision, and why.
   * Derived purely from persisted state + presence, so it is safe to recompute
   * at any time (reconnects, alarms, hibernation wake-ups).
   */
  private plan(): { due: number; kind: "bot" | "timeout" | "forced" } | null {
    const room = this.room!;
    const gd = room.game;
    const g = this.game;
    if (!gd || !g || g.isDone()) return null;
    // Pause entirely while nobody is connected; resume when someone returns.
    if (!this.sockets().some((ws) => this.attachment(ws).cid)) return null;
    const active = g.activePlayer();
    const phase = g.phase();
    const s = gd.seats[active];
    const botPace = gd.decisionSince + this.botDelay(phase, gd.step);
    if (!s.cid || s.bot || s.left) return { due: botPace, kind: "bot" };
    const off = room.offlineSince[s.cid];
    if (off != null && this.socketsFor(s.cid).length === 0) {
      return { due: Math.max(off + AWAY_GRACE_MS, botPace), kind: "bot" };
    }
    if (phase === PHASE_LOSE_CARD && g.validActions().length === 1) {
      return { due: gd.decisionSince + FORCED_MOVE_DELAY_MS, kind: "forced" };
    }
    if (room.rules.responseTimer && (phase === PHASE_CHALLENGE_ACTION || phase === PHASE_BLOCK || phase === PHASE_CHALLENGE_BLOCK)) {
      return { due: gd.decisionSince + RESPONSE_TIMEOUT_MS, kind: "timeout" };
    }
    return null;
  }

  private rescheduleDecision(): void {
    const gd = this.room?.game;
    if (gd) gd.due = this.plan()?.due ?? null;
  }

  /** Act for the current decision if the plan says it's due. */
  private actIfDue(now: number): void {
    const gd = this.room!.game!;
    const g = this.game!;
    const p = this.plan();
    if (!p || now < p.due - 10) return;
    const active = g.activePlayer();
    const phase = g.phase();
    const valid = g.validActions();
    let action: number;
    if (p.kind !== "bot") {
      // Response timer ran out (pass), or only one legal choice.
      action = valid.includes(ACTION_PASS) ? ACTION_PASS : valid[0];
      if (p.kind === "timeout") this.log(`${gd.seats[active].name} ran out of time.`, "info", active);
    } else {
      const s = gd.seats[active];
      action = chooseBotAction(s.bot ?? "medium", this.botView(active));
      if (!g.isValid(action)) action = valid.includes(ACTION_PASS) ? ACTION_PASS : valid[0];
      if (s.cid && !s.left && phase === PHASE_MAIN_ACTION) this.log(`${s.name} is away — a bot is playing for them.`, "info", active);
    }
    this.apply(action);
    // Bots finish an exchange with their second discard right away.
    if (!g.isDone() && phase === PHASE_EXCHANGE_DISCARD && g.phase() === PHASE_EXCHANGE_DISCARD && g.activePlayer() === active) {
      const s = gd.seats[active];
      let second = chooseBotAction(s.bot ?? "medium", this.botView(active));
      if (!g.isValid(second)) second = g.validActions()[0];
      this.apply(second);
    }
    this.afterChange();
  }

  private botView(seat: number): BotView {
    const g = this.game!;
    const gd = this.room!.game!;
    const n = gd.seats.length;
    const revealed = [0, 0, 0, 0, 0];
    for (let p = 0; p < n; p++) {
      for (const s of [0, 1] as const) if (!g.cardAlive(p, s)) revealed[g.cardType(p, s)]++; // dead cards are face-up
    }
    const slots: (number | null)[] = [
      g.cardAlive(seat, 0) ? g.cardType(seat, 0) : null,
      g.cardAlive(seat, 1) ? g.cardType(seat, 1) : null,
      null,
      null,
    ];
    if (g.phase() === PHASE_EXCHANGE_DISCARD && g.activePlayer() === seat) {
      const [a, b] = g.exchangeCards();
      slots[2] = a;
      slots[3] = b;
    }
    return {
      seat,
      numPlayers: n,
      phase: g.phase(),
      turnPlayer: g.turnPlayer(),
      pending: g.pendingAction(),
      blocker: g.blocker(),
      blockCard: g.blockCard(),
      actions: g.validActions(),
      slots,
      coins: Array.from({ length: n }, (_, p) => g.coins(p)),
      influence: Array.from({ length: n }, (_, p) => g.influence(p)),
      revealed,
      claims: gd.claims.map((c) => new Set(c)),
    };
  }

  async alarm(): Promise<void> {
    const room = this.room;
    if (!room) return;
    const now = Date.now();
    const connected = this.sockets().length > 0;

    if (!connected && now - room.lastSeen > EMPTY_ROOM_TTL_MS) {
      await this.ctx.storage.deleteAll();
      this.room = null;
      this.game = null;
      return;
    }
    if (connected) room.lastSeen = now;

    if (room.game && this.game) {
      try {
        this.actIfDue(now);
      } catch (e) {
        console.error("[GameRoom] auto-move failed:", (e as Error).message, (e as Error).stack);
      }
      this.rescheduleDecision();
    }

    if (!room.game) {
      // Drop humans who disconnected from the lobby and never came back.
      const before = room.seats.length;
      room.seats = room.seats.filter((s) => !s.cid || room.offlineSince[s.cid] == null || now - room.offlineSince[s.cid] < LOBBY_DROP_MS);
      if (room.seats.length !== before) {
        for (const cid of Object.keys(room.offlineSince)) if (!room.seats.some((s) => s.cid === cid)) delete room.offlineSince[cid];
        this.ensureHost();
        this.broadcast();
      }
    }
    this.save();
    await this.scheduleAlarm();
  }

  private async scheduleAlarm(): Promise<void> {
    const room = this.room;
    if (!room) return;
    const now = Date.now();
    const times: number[] = [];
    if (room.game) {
      this.rescheduleDecision();
      if (room.game.due != null) times.push(room.game.due);
    } else {
      for (const s of room.seats) {
        const off = s.cid ? room.offlineSince[s.cid] : undefined;
        if (off != null) times.push(off + LOBBY_DROP_MS);
      }
    }
    if (this.sockets().length === 0) times.push(room.lastSeen + EMPTY_ROOM_TTL_MS + 1000);
    else times.push(now + EMPTY_ROOM_TTL_MS); // keep a long-stop alarm so idle rooms eventually expire
    const next = Math.max(now, Math.min(...times));
    const current = await this.ctx.storage.getAlarm();
    if (current !== next) await this.ctx.storage.setAlarm(next);
  }

  // ---------------------------------------------------------------------------
  // Views

  private broadcast(except?: WebSocket): void {
    for (const ws of this.sockets()) {
      if (ws === except) continue;
      if (this.room?.game) this.sendState(ws);
      else this.sendLobby(ws);
    }
  }

  private sendLobby(ws: WebSocket): void {
    const room = this.room!;
    const { cid } = this.attachment(ws);
    const seats: LobbySeat[] = room.seats.map((s) => ({
      name: s.name,
      bot: s.bot,
      online: !s.cid || this.socketsFor(s.cid).length > 0,
      host: !!s.cid && s.cid === room.hostCid,
      you: !!cid && s.cid === cid,
    }));
    this.send(ws, { type: "lobby", code: room.code, seats, rules: room.rules, isHost: !!cid && cid === room.hostCid, maxSeats: MAX_SEATS });
  }

  private sendState(ws: WebSocket, fullHistory = false): void {
    const room = this.room!;
    const gd = room.game;
    const g = this.game;
    if (!gd || !g) return this.sendLobby(ws);
    const { cid } = this.attachment(ws);
    const you = cid ? gd.seats.findIndex((s) => s.cid === cid && !s.left) : -1;
    const names = gd.seats.map((s) => s.name);
    const phase = g.phase();
    const active = g.activePlayer();
    const tp = g.turnPlayer();
    const done = g.isDone();
    const pending = phase === PHASE_MAIN_ACTION ? undefined : g.pendingAction();

    const players: PlayerView[] = gd.seats.map((s, i) => {
      const revealed: number[] = [];
      for (const slot of [0, 1] as const) if (!g.cardAlive(i, slot)) revealed.push(g.cardType(i, slot));
      const human = !!s.cid;
      const online = !human || this.socketsFor(s.cid!).length > 0;
      return {
        name: s.name,
        coins: g.coins(i),
        influence: g.influence(i),
        revealed,
        bot: !!s.bot,
        online,
        away: this.isAway(s),
        alive: g.isAlive(i),
        claims: (gd.held?.[i] ?? []).map((r) => CARD_NAMES[r]),
      };
    });

    const cards = you >= 0 ? ([0, 1] as const).map((s) => ({ type: g.cardType(you, s), alive: g.cardAlive(you, s) })) : [];
    const drawn = you >= 0 && phase === PHASE_EXCHANGE_DISCARD && active === you ? g.exchangeCards() : undefined;
    const actions = you >= 0 && active === you && !done
      ? g.validActions().map((id) => ({ id, label: actionLabel(id, names, you) }))
      : [];

    // Countdown shown for timers that matter to people: your own response timer,
    // or how long until a disconnected player is auto-played.
    let deadlineMs: number | undefined;
    const p = done ? null : this.plan();
    const activeSeat = gd.seats[active];
    if (p && p.kind === "timeout") deadlineMs = Math.max(0, p.due - Date.now());
    else if (p && p.kind === "bot" && activeSeat?.cid && !activeSeat.left && !this.isAway(activeSeat)) deadlineMs = Math.max(0, p.due - Date.now());

    const histBase = fullHistory ? 0 : Math.max(0, gd.history.length - HISTORY_TAIL);
    const view: GameView = {
      type: "state",
      code: room.code,
      you,
      cards,
      drawn,
      players,
      phase: PHASE_NAMES[phase] ?? "unknown",
      active,
      turnPlayer: tp,
      pending,
      block: phase === PHASE_CHALLENGE_BLOCK ? { seat: g.blocker(), card: g.blockCard() } : undefined,
      actions,
      prompt: this.prompt(you, names),
      deadlineMs,
      history: gd.history.slice(histBase),
      historyBase: histBase,
      deck: g.deckTotal(),
      shuffledIn: gd.shuffledIn && (!gd.shuffledIn.private || gd.shuffledIn.seat === you)
        ? { cards: gd.shuffledIn.cards, seat: gd.shuffledIn.seat, turn: gd.shuffledIn.turn, via: gd.shuffledIn.via }
        : undefined,
      turn: gd.turn,
      step: gd.step,
      isHost: !!cid && cid === room.hostCid,
      rules: room.rules,
    };
    this.send(ws, view);
  }

  /** One-line description of what's happening, from the viewer's perspective. */
  private prompt(you: number, names: string[]): string {
    const g = this.game!;
    const phase = g.phase();
    const active = g.activePlayer();
    const tp = g.turnPlayer();
    const pa = g.pendingAction();
    const me = active === you;
    const Name = (s: number) => (s === you ? "You" : names[s]);
    const name = (s: number) => (s === you ? "you" : names[s]);
    const t = actionTarget(pa);

    const describe = (): string => {
      if (pa === ACTION_FOREIGN_AID) return `${Name(tp)} ${tp === you ? "take" : "takes"} Foreign Aid`;
      if (pa === ACTION_TAX) return `${Name(tp)} ${tp === you ? "claim" : "claims"} Duke to take Tax`;
      if (pa === ACTION_EXCHANGE) return `${Name(tp)} ${tp === you ? "claim" : "claims"} Ambassador to Exchange`;
      if (isSteal(pa)) return `${Name(tp)} ${tp === you ? "claim" : "claims"} Captain to steal from ${name(t!)}`;
      if (isAssassinate(pa)) return `${Name(tp)} ${tp === you ? "claim" : "claims"} Assassin to assassinate ${name(t!)}`;
      if (isCoup(pa)) return `${Name(tp)} ${tp === you ? "Coup" : "Coups"} ${name(t!)}`;
      return `${Name(tp)} ${tp === you ? "act" : "acts"}`;
    };

    switch (phase) {
      case PHASE_MAIN_ACTION:
        if (me) return g.coins(you) >= 10 ? "You have 10+ coins — you must Coup." : "Your turn — choose an action.";
        return `${names[tp]} is choosing an action…`;
      case PHASE_CHALLENGE_ACTION:
        return me ? `${describe()}. Challenge the claim?` : `${describe()}. Waiting for ${names[active]} to challenge or pass…`;
      case PHASE_BLOCK: {
        if (pa === ACTION_FOREIGN_AID) return me ? `${describe()}. Block it by claiming Duke?` : `${describe()}. Waiting for ${names[active]} to block or pass…`;
        return me ? `${describe()}. Block?` : `${describe()}. Waiting for ${names[active]} to block or allow…`;
      }
      case PHASE_CHALLENGE_BLOCK: {
        const b = g.blocker();
        const head = `${Name(b)} ${b === you ? "block" : "blocks"} by claiming ${CARD_NAMES[g.blockCard()]}`;
        return me ? `${head}. Challenge the block?` : `${head}. Waiting for ${names[active]} to challenge or pass…`;
      }
      case PHASE_LOSE_CARD:
        return me ? "You lose an influence — choose a card to reveal." : `${names[active]} must reveal a card…`;
      case PHASE_EXCHANGE_DISCARD:
        return me ? "Exchange — choose which cards to keep." : `${names[active]} is exchanging cards…`;
      default:
        return "";
    }
  }

  // ---------------------------------------------------------------------------
  // Helpers

  private attachment(ws: WebSocket): Attachment {
    try {
      return (ws.deserializeAttachment() as Attachment | null) ?? { cid: null, name: null };
    } catch {
      return { cid: null, name: null };
    }
  }

  /** Open sockets (excluding ones mid-close). */
  private sockets(): WebSocket[] {
    return this.ctx.getWebSockets().filter((ws) => !this.closing.has(ws));
  }

  private socketsFor(cid: string, except?: WebSocket): WebSocket[] {
    return this.sockets().filter((ws) => ws !== except && this.attachment(ws).cid === cid);
  }

  private isSeated(cid: string): boolean {
    const room = this.room!;
    return room.seats.some((s) => s.cid === cid) || !!room.game?.seats.some((s) => s.cid === cid);
  }

  private save(): void {
    if (!this.room) return;
    if (this.room.game && this.game) this.room.game.bytes = this.game.bytes;
    void this.ctx.storage.put("room", this.room);
  }

  private send(ws: WebSocket, msg: ServerMessage): void {
    try {
      ws.send(JSON.stringify(msg));
    } catch {
      // socket already closed
    }
  }
}

export { CID_RE };
