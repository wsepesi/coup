// Matchmaker Durable Object
// Singleton that manages room creation, join codes, and quick-match queue.

import type { Env, BotDifficulty } from "./types.js";

interface RoomInfo {
  code: string;
  gameId: string;
  numPlayers: number;
  numBots: number;
  botDifficulty: BotDifficulty;
  currentHumans: number;
  started: boolean;
  createdAt: number;
}

interface QueueEntry {
  ws: WebSocket;
  username: string;
  enqueuedAt: number;
}

function generateCode(): string {
  const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no I/O/0/1 for clarity
  let code = "";
  for (let i = 0; i < 6; i++) {
    code += chars[Math.floor(Math.random() * chars.length)];
  }
  return code;
}

export class Matchmaker implements DurableObject {
  private state: DurableObjectState;
  private env: Env;
  private rooms = new Map<string, RoomInfo>();
  private quickQueue: QueueEntry[] = [];

  constructor(state: DurableObjectState, env: Env) {
    this.state = state;
    this.env = env;
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/ws") {
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.state.acceptWebSocket(server);
      return new Response(null, { status: 101, webSocket: client });
    }

    return new Response("Not found", { status: 404 });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== "string") return;

    let msg: any;
    try {
      msg = JSON.parse(message);
    } catch {
      this.send(ws, { type: "error", message: "Invalid JSON" });
      return;
    }

    switch (msg.type) {
      case "create":
        await this.handleCreate(ws, msg);
        break;
      case "join":
        await this.handleJoin(ws, msg);
        break;
      case "quick_play":
        this.handleQuickPlay(ws, msg);
        break;
      default:
        this.send(ws, { type: "error", message: `Unknown message type: ${msg.type}` });
    }
  }

  async webSocketClose(ws: WebSocket): Promise<void> {
    // Remove from quick queue if present
    this.quickQueue = this.quickQueue.filter(e => e.ws !== ws);
  }

  async webSocketError(ws: WebSocket): Promise<void> {
    this.quickQueue = this.quickQueue.filter(e => e.ws !== ws);
  }

  private async handleCreate(ws: WebSocket, msg: any): Promise<void> {
    const { username, numPlayers, numBots, botDifficulty } = msg;

    if (!username || typeof username !== "string") {
      this.send(ws, { type: "error", message: "Username required" });
      return;
    }

    const np = Math.max(2, Math.min(6, Number(numPlayers) || 4));
    const nb = Math.max(0, Math.min(np - 1, Number(numBots) || 0));
    const diff: BotDifficulty = botDifficulty === "easy" ? "easy" : "medium";

    // Generate unique code
    let code: string;
    do {
      code = generateCode();
    } while (this.rooms.has(code));

    // Create a GameRoom DO with a unique ID based on the code
    const gameId = this.env.GAME.idFromName(code);
    const gameStub = this.env.GAME.get(gameId);

    // Initialize the room via an HTTP call
    const initRes = await gameStub.fetch(new Request("https://internal/init", {
      method: "POST",
      body: JSON.stringify({
        code,
        numPlayers: np,
        numBots: nb,
        botDifficulty: diff,
        hostUsername: username,
      }),
    }));

    if (!initRes.ok) {
      this.send(ws, { type: "error", message: "Failed to create room" });
      return;
    }

    const room: RoomInfo = {
      code,
      gameId: gameId.toString(),
      numPlayers: np,
      numBots: nb,
      botDifficulty: diff,
      currentHumans: 0,
      started: false,
      createdAt: Date.now(),
    };

    this.rooms.set(code, room);

    this.send(ws, { type: "room_created", code });
  }

  private async handleJoin(ws: WebSocket, msg: any): Promise<void> {
    const { username, code } = msg;

    if (!username || typeof username !== "string") {
      this.send(ws, { type: "error", message: "Username required" });
      return;
    }

    const upperCode = (code || "").toString().toUpperCase().trim();
    const room = this.rooms.get(upperCode);

    if (!room) {
      this.send(ws, { type: "error", message: "Room not found" });
      return;
    }

    if (room.started) {
      this.send(ws, { type: "error", message: "Game already started" });
      return;
    }

    // Tell the client to connect to the game room
    this.send(ws, { type: "room_created", code: upperCode });
  }

  private handleQuickPlay(ws: WebSocket, msg: any): void {
    const { username } = msg;
    if (!username || typeof username !== "string") {
      this.send(ws, { type: "error", message: "Username required" });
      return;
    }

    // Add to queue
    this.quickQueue.push({ ws, username, enqueuedAt: Date.now() });

    // Try to match: if we have 2+ players, create a room
    if (this.quickQueue.length >= 2) {
      this.matchQuickPlay();
    } else {
      this.send(ws, { type: "error", message: "Waiting for more players..." });
    }
  }

  private async matchQuickPlay(): Promise<void> {
    // Take 2 players from the queue and create a 4-player game with 2 bots
    const players = this.quickQueue.splice(0, 2);

    let code: string;
    do {
      code = generateCode();
    } while (this.rooms.has(code));

    const numPlayers = 4;
    const numBots = 2;
    const botDifficulty: BotDifficulty = "medium";

    const gameId = this.env.GAME.idFromName(code);
    const gameStub = this.env.GAME.get(gameId);

    const initRes = await gameStub.fetch(new Request("https://internal/init", {
      method: "POST",
      body: JSON.stringify({
        code,
        numPlayers,
        numBots,
        botDifficulty,
        hostUsername: players[0].username,
      }),
    }));

    if (!initRes.ok) {
      for (const p of players) {
        this.send(p.ws, { type: "error", message: "Failed to create quick match" });
      }
      return;
    }

    const room: RoomInfo = {
      code,
      gameId: gameId.toString(),
      numPlayers,
      numBots,
      botDifficulty,
      currentHumans: 0,
      started: false,
      createdAt: Date.now(),
    };
    this.rooms.set(code, room);

    // Tell both players to connect to the game room
    for (const p of players) {
      this.send(p.ws, { type: "room_created", code });
    }
  }

  private send(ws: WebSocket, msg: object): void {
    try {
      ws.send(JSON.stringify(msg));
    } catch {
      // WebSocket already closed
    }
  }
}
