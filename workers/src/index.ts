// Worker entry point — room creation (HTTP) and WebSocket routing to GameRoom DOs.

import type { Env, BotDifficulty } from "./types.js";
import { PROTOCOL, isDifficulty } from "./types.js";
import { CID_RE, MAX_SEATS, type InitBody } from "./game-room.js";
import { PROTOCOL_VERSION } from "./version.js";

export { GameRoom } from "./game-room.js";

const CORS_HEADERS: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type",
  "Access-Control-Max-Age": "86400",
};

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...CORS_HEADERS, "Content-Type": "application/json" },
  });
}

// No I/O/0/1 to keep codes easy to read aloud and type.
const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const CODE_LEN = 5;

function generateCode(): string {
  const bytes = new Uint8Array(CODE_LEN);
  crypto.getRandomValues(bytes);
  let code = "";
  for (const b of bytes) code += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return code;
}

async function createRoom(request: Request, env: Env): Promise<Response> {
  let body: any;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid JSON" }, 400);
  }
  if (typeof body?.cid !== "string" || !CID_RE.test(body.cid)) return json({ error: "Invalid client id" }, 400);
  const bots: BotDifficulty[] = Array.isArray(body.bots)
    ? body.bots.slice(0, MAX_SEATS - 1).map((d: unknown) => (isDifficulty(d) ? d : "medium"))
    : [];

  for (let attempt = 0; attempt < 8; attempt++) {
    const code = generateCode();
    const stub = env.GAME.get(env.GAME.idFromName(code));
    const init: InitBody = { code, hostCid: body.cid, rules: body.rules, bots, autoStart: !!body.autoStart };
    const res = await stub.fetch("https://room/init", { method: "POST", body: JSON.stringify(init) });
    if (res.ok) return json({ code });
    if (res.status !== 409) return json({ error: "Failed to create room" }, 500);
    // 409: code collision with a live room — try another.
  }
  return json({ error: "Could not allocate a room code" }, 503);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: CORS_HEADERS });

    if (url.pathname === "/api/health") {
      return json({ status: "ok", protocol: PROTOCOL, version: PROTOCOL_VERSION, timestamp: Date.now() });
    }

    if (url.pathname === "/api/rooms" && request.method === "POST") {
      return createRoom(request, env);
    }

    const gameMatch = url.pathname.match(/^\/ws\/game\/([A-Za-z0-9]{4,8})$/);
    if (gameMatch) {
      if (request.headers.get("Upgrade") !== "websocket") {
        return new Response("Expected WebSocket", { status: 426, headers: CORS_HEADERS });
      }
      const code = gameMatch[1].toUpperCase();
      const stub = env.GAME.get(env.GAME.idFromName(code));
      return stub.fetch(new Request("https://room/ws", request));
    }

    return new Response("Not found", { status: 404, headers: CORS_HEADERS });
  },
};
