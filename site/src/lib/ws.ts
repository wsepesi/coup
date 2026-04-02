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
  onConnect?: (send: (msg: ClientMessage) => void) => void;
}

export function useWebSocket({ path, enabled = true, onMessage, onConnect }: UseWebSocketOptions) {
  const wsRef = useRef<WebSocket | null>(null);
  const [status, setStatus] = useState<ConnectionStatus>("disconnected");
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const reconnectAttempts = useRef(0);
  const disposed = useRef(false);
  const onMessageRef = useRef(onMessage);
  onMessageRef.current = onMessage;
  const onConnectRef = useRef(onConnect);
  onConnectRef.current = onConnect;

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
      // Ignore if this WS is already stale
      if (wsRef.current !== ws) return;
      setStatus("connected");
      reconnectAttempts.current = 0;
      onConnectRef.current?.((msg: ClientMessage) => {
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify(msg));
        }
      });
    };

    ws.onmessage = (event) => {
      // Ignore messages from stale connections
      if (wsRef.current !== ws) return;
      try {
        const msg = JSON.parse(event.data) as ServerMessage;
        onMessageRef.current?.(msg);
      } catch {
        // ignore malformed messages
      }
    };

    ws.onclose = () => {
      // Ignore if this is a stale WS (a newer one has replaced it)
      if (wsRef.current !== ws) return;
      wsRef.current = null;

      // Don't reconnect if disposed (cleanup was called)
      if (disposed.current) {
        setStatus("disconnected");
        return;
      }

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
    disposed.current = true;
    const ws = wsRef.current;
    wsRef.current = null;
    ws?.close();
    setStatus("disconnected");
  }, []);

  useEffect(() => {
    disposed.current = false;
    connect();
    return () => {
      disconnect();
    };
  }, [connect, disconnect]);

  return { status, send, disconnect };
}
