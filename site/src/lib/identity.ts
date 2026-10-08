// Per-browser identity and server endpoints.

const CID_KEY = "coup_cid";
const NAME_KEY = "coup_username";

function storage(): Storage | null {
  try {
    return typeof window === "undefined" ? null : window.localStorage;
  } catch {
    return null; // private mode / blocked storage
  }
}

let memoryCid: string | null = null;

/**
 * Secret id that binds this browser to its seat. Never shown to other players;
 * lets a refresh or a reopened tab reclaim the same seat.
 */
export function getClientId(): string {
  const s = storage();
  let cid = s?.getItem(CID_KEY) ?? memoryCid;
  if (!cid || !/^[A-Za-z0-9_-]{16,64}$/.test(cid)) {
    // getRandomValues works in insecure contexts too (randomUUID doesn't, e.g. LAN testing).
    cid = Array.from(crypto.getRandomValues(new Uint8Array(16)), (b) => b.toString(16).padStart(2, "0")).join("");
    try { s?.setItem(CID_KEY, cid); } catch { /* ignore */ }
  }
  memoryCid = cid;
  return cid;
}

export function getSavedName(): string {
  return storage()?.getItem(NAME_KEY) ?? "";
}

export function saveName(name: string): void {
  try { storage()?.setItem(NAME_KEY, name); } catch { /* ignore */ }
}

const RAW_BASE = process.env.NEXT_PUBLIC_WS_URL ?? "wss://coup-server.sepesi-coup.workers.dev";

function normalize(base: string, ws: boolean): string {
  let b = base.replace(/\/+$/, "");
  if (!/^(wss?|https?):\/\//.test(b)) b = "wss://" + b;
  if (ws) return b.replace(/^http/, "ws");
  return b.replace(/^ws/, "http");
}

export const WS_BASE = normalize(RAW_BASE, true);
export const HTTP_BASE = normalize(RAW_BASE, false);

export interface CreateRoomOptions {
  bots?: string[];
  autoStart?: boolean;
  rules?: { refundOnChallenge?: boolean; responseTimer?: boolean };
}

export async function createRoom(opts: CreateRoomOptions = {}): Promise<string> {
  let res: Response;
  try {
    res = await fetch(`${HTTP_BASE}/api/rooms`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ cid: getClientId(), ...opts }),
    });
  } catch {
    throw new Error("Can't reach the game server. Check your connection and try again.");
  }
  const body = (await res.json().catch(() => ({}))) as { code?: string; error?: string };
  if (!res.ok || !body.code) throw new Error(body.error ?? `Server error (${res.status})`);
  return body.code;
}

/** Room codes are 4-8 chars from an unambiguous alphabet; accept pasted links too. */
export function parseRoomCode(input: string): string | null {
  const m = input.trim().match(/(?:\/(?:lobby|game)\/)?([A-Za-z0-9]{4,8})\/?(?:[?#].*)?$/);
  return m ? m[1].toUpperCase() : null;
}
