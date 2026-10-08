"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { WS_BASE, getClientId } from "./identity";
import {
  PROTOCOL,
  type ClientMessage,
  type GameResult,
  type GameState,
  type HistoryEntry,
  type LobbyView,
  type ServerMessage,
} from "./types";

export type ConnectionStatus = "connecting" | "connected" | "reconnecting" | "closed";

/** Close codes the server uses for "don't reconnect" situations. */
const TERMINAL_CLOSE: Record<number, string> = {
  4000: "You left the room.",
  4001: "This game is open in another tab.",
  4003: "The host removed you from the room.",
  4004: "Room not found. It may have expired.",
};

const PING_MS = 25_000;
const DEAD_AFTER_MS = 60_000;

export interface RoomConnection {
  status: ConnectionStatus;
  lobby: LobbyView | null;
  game: GameState | null;
  result: GameResult | null;
  /** Transient (non-fatal) server error, cleared on the next state change. */
  error: string | null;
  /** Why the connection ended for good (kicked, replaced, room gone). */
  closedReason: string | null;
  outdated: boolean;
  send: (msg: ClientMessage) => void;
  reconnect: () => void;
  dismissResult: () => void;
  clearError: () => void;
}

/**
 * Single WebSocket for the lifetime of the room page. Handles join/rejoin,
 * heartbeats, backoff reconnects, and folding server messages into state.
 */
export function useRoom(code: string, name: string | null): RoomConnection {
  const [status, setStatus] = useState<ConnectionStatus>("connecting");
  const [lobby, setLobby] = useState<LobbyView | null>(null);
  const [game, setGame] = useState<GameState | null>(null);
  const [result, setResult] = useState<GameResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [closedReason, setClosedReason] = useState<string | null>(null);
  const [outdated, setOutdated] = useState(false);
  const [generation, setGeneration] = useState(0);

  const wsRef = useRef<WebSocket | null>(null);
  const historyRef = useRef<HistoryEntry[]>([]);
  const nameRef = useRef(name);
  nameRef.current = name;

  const onMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "welcome":
        if (msg.protocol !== PROTOCOL) setOutdated(true);
        break;
      case "lobby":
        historyRef.current = [];
        setLobby(msg);
        setGame(null);
        setError(null);
        break;
      case "state": {
        const h = historyRef.current;
        const merged = msg.historyBase <= h.length ? h.slice(0, msg.historyBase).concat(msg.history) : msg.history;
        historyRef.current = merged;
        const { history: _h, historyBase: _b, deadlineMs, ...rest } = msg;
        void _h; void _b;
        setGame({ ...rest, history: merged, deadline: deadlineMs != null ? Date.now() + deadlineMs : null });
        setLobby(null);
        setResult((r) => (msg.turn <= 1 && r ? null : r));
        setError(null);
        break;
      }
      case "game_over": {
        const { type: _t, ...res } = msg;
        void _t;
        setResult(res);
        break;
      }
      case "error":
        if (msg.fatal) setClosedReason(msg.message);
        else setError(msg.message);
        break;
    }
  }, []);

  useEffect(() => {
    if (!name || !code) return;
    let disposed = false;
    let attempts = 0;
    let retryTimer: ReturnType<typeof setTimeout> | null = null;
    let pingTimer: ReturnType<typeof setInterval> | null = null;
    let lastHeard = Date.now();

    const clearTimers = () => {
      if (retryTimer) clearTimeout(retryTimer);
      if (pingTimer) clearInterval(pingTimer);
      retryTimer = pingTimer = null;
    };

    const connect = () => {
      if (disposed) return;
      clearTimers();
      setStatus(attempts === 0 ? "connecting" : "reconnecting");
      const ws = new WebSocket(`${WS_BASE}/ws/game/${encodeURIComponent(code)}`);
      wsRef.current = ws;

      ws.onopen = () => {
        if (wsRef.current !== ws) return;
        attempts = 0;
        lastHeard = Date.now();
        setStatus("connected");
        ws.send(JSON.stringify({ type: "join", name: nameRef.current ?? "Player", cid: getClientId() }));
        // Always heartbeat (background tabs included); the server answers without waking up.
        pingTimer = setInterval(() => {
          if (Date.now() - lastHeard > DEAD_AFTER_MS) {
            ws.close(); // half-open connection; onclose schedules a reconnect
            return;
          }
          if (ws.readyState === WebSocket.OPEN) ws.send('{"type":"ping"}');
        }, PING_MS);
      };

      ws.onmessage = (ev) => {
        if (wsRef.current !== ws) return;
        lastHeard = Date.now();
        let msg: ServerMessage;
        try {
          msg = JSON.parse(ev.data as string);
        } catch {
          return;
        }
        onMessage(msg);
      };

      ws.onclose = (ev) => {
        if (wsRef.current !== ws) return;
        wsRef.current = null;
        clearTimers();
        if (disposed) return;
        const terminal = TERMINAL_CLOSE[ev.code];
        if (terminal) {
          setClosedReason((r) => r ?? terminal);
          setStatus("closed");
          return;
        }
        const delay = Math.min(500 * 2 ** attempts, 8000) * (0.75 + Math.random() * 0.5);
        attempts++;
        setStatus("reconnecting");
        retryTimer = setTimeout(connect, delay);
      };

      ws.onerror = () => {
        // onclose follows and handles retry
      };
    };

    // Reconnect promptly when the tab wakes up or the network returns.
    const kick = () => {
      if (disposed || document.visibilityState !== "visible") return;
      const ws = wsRef.current;
      if (!ws || ws.readyState === WebSocket.CLOSED) {
        attempts = Math.min(attempts, 1);
        connect();
      }
    };
    document.addEventListener("visibilitychange", kick);
    window.addEventListener("online", kick);

    setClosedReason(null);
    connect();
    return () => {
      disposed = true;
      clearTimers();
      document.removeEventListener("visibilitychange", kick);
      window.removeEventListener("online", kick);
      const ws = wsRef.current;
      wsRef.current = null;
      ws?.close(1000);
    };
  }, [code, name, generation, onMessage]);

  const send = useCallback((msg: ClientMessage) => {
    const ws = wsRef.current;
    if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
  }, []);

  const reconnect = useCallback(() => {
    setClosedReason(null);
    setGeneration((g) => g + 1);
  }, []);

  const dismissResult = useCallback(() => setResult(null), []);
  const clearError = useCallback(() => setError(null), []);

  return { status, lobby, game, result, error, closedReason, outdated, send, reconnect, dismissResult, clearError };
}
