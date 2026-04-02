// Worker entry point — routes requests to Durable Objects with CORS support.

import type { Env } from "./types.js";

export { Matchmaker } from "./matchmaker.js";
export { GameRoom } from "./game-room.js";

const CORS_HEADERS: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Upgrade, Sec-WebSocket-Key, Sec-WebSocket-Version, Sec-WebSocket-Protocol",
};

function corsResponse(body: string | null, status: number, extraHeaders?: Record<string, string>): Response {
  return new Response(body, {
    status,
    headers: { ...CORS_HEADERS, ...extraHeaders },
  });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    // CORS preflight
    if (request.method === "OPTIONS") {
      return corsResponse(null, 204);
    }

    // Health check
    if (url.pathname === "/api/health") {
      return corsResponse(JSON.stringify({ status: "ok", timestamp: Date.now() }), 200, {
        "Content-Type": "application/json",
      });
    }

    // WebSocket: Matchmaker
    if (url.pathname === "/ws/matchmaker") {
      const upgradeHeader = request.headers.get("Upgrade");
      if (upgradeHeader !== "websocket") {
        return corsResponse("Expected WebSocket", 426);
      }
      const id = env.MATCHMAKER.idFromName("singleton");
      const stub = env.MATCHMAKER.get(id);
      // Build a clean WebSocket upgrade request to the DO
      return stub.fetch("https://do/ws", { headers: { Upgrade: "websocket" } });
    }

    // WebSocket: Game Room by code
    const gameMatch = url.pathname.match(/^\/ws\/game\/([A-Za-z0-9]+)$/);
    if (gameMatch) {
      const upgradeHeader = request.headers.get("Upgrade");
      if (upgradeHeader !== "websocket") {
        return corsResponse("Expected WebSocket", 426);
      }
      const code = gameMatch[1].toUpperCase();
      const id = env.GAME.idFromName(code);
      const stub = env.GAME.get(id);
      return stub.fetch("https://do/ws", { headers: { Upgrade: "websocket" } });
    }

    return corsResponse("Not found", 404);
  },
};
