"use client";

import { useEffect, useRef, useState, useCallback } from "react";
import type { ServerMessage, ClientMessage } from "./types";

const BASE_URL =
  process.env.NEXT_PUBLIC_WS_URL ?? "wss://coup-server.sepesi-coup.workers.dev";

export type ConnectionStatus = "connecting" | "connected" | "disconnected" | "reconnecting";

interface UseWebSocketOptions {
  /** WS path relative to base, e.g. "/ws/matchmaker" or "/ws/game/ABCD12" */
  path: string;
  /** If false/undefined, don't connect yet */
  enabled?: boolean;
  onMessage?: (msg: ServerMessage) => void;
}

export function useWebSocket({ path, enabled = true, onMessage }: UseWebSocketOptions) {
  const wsRef = useRef<WebSocket | null>(null);
  const [status, setStatus] = useState<ConnectionStatus>("disconnected");
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const reconnectAttempts = useRef(0);
  const onMessageRef = useRef(onMessage);
  onMessageRef.current = onMessage;

  const connect = useCallback(() => {
    if (!enabled) return;

    // Convert http(s) to ws(s) if needed
    let wsBase = BASE_URL;
    if (wsBase.startsWith("https://")) wsBase = "wss://" + wsBase.slice(8);
    else if (wsBase.startsWith("http://")) wsBase = "ws://" + wsBase.slice(7);
    if (!wsBase.startsWith("ws")) wsBase = "wss://" + wsBase;

    const url = `${wsBase}${path}`;
    setStatus("connecting");

    const ws = new WebSocket(url);
    wsRef.current = ws;

    ws.onopen = () => {
      setStatus("connected");
      reconnectAttempts.current = 0;
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data) as ServerMessage;
        onMessageRef.current?.(msg);
      } catch {
        // ignore malformed messages
      }
    };

    ws.onclose = () => {
      setStatus("disconnected");
      wsRef.current = null;

      // Reconnect with exponential backoff (max 10s)
      const delay = Math.min(1000 * 2 ** reconnectAttempts.current, 10000);
      reconnectAttempts.current++;
      setStatus("reconnecting");
      reconnectTimer.current = setTimeout(() => {
        connect();
      }, delay);
    };

    ws.onerror = () => {
      ws.close();
    };
  }, [path, enabled]);

  const send = useCallback((msg: ClientMessage) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg));
    }
  }, []);

  const disconnect = useCallback(() => {
    if (reconnectTimer.current) {
      clearTimeout(reconnectTimer.current);
      reconnectTimer.current = null;
    }
    reconnectAttempts.current = 0;
    wsRef.current?.close();
    wsRef.current = null;
    setStatus("disconnected");
  }, []);

  useEffect(() => {
    connect();
    return () => {
      disconnect();
    };
  }, [connect, disconnect]);

  return { status, send, disconnect };
}
