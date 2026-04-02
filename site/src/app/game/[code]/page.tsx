"use client";

import { useState, useCallback, useEffect, useRef } from "react";
import { useParams, useRouter } from "next/navigation";
import { useWebSocket, type ConnectionStatus } from "@/lib/ws";
import type { ServerMessage, GameState, Standing } from "@/lib/types";
import GameBoard from "@/components/GameBoard";

export default function GamePage() {
  const params = useParams();
  const router = useRouter();
  const code = params.code as string;

  const [gameState, setGameState] = useState<GameState | null>(null);
  const [gameOver, setGameOver] = useState<{
    winner: number;
    winnerName: string;
    finalStandings: Standing[];
  } | null>(null);
  const [error, setError] = useState<string | null>(null);

  const onMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "state":
        setGameState({
          yourSeat: msg.yourSeat,
          yourCards: msg.yourCards,
          players: msg.players,
          phase: msg.phase,
          activePlayer: msg.activePlayer,
          isYourTurn: msg.isYourTurn,
          availableActions: msg.availableActions,
          history: msg.history,
          context: msg.context,
          pendingAction: msg.pendingAction,
          deckSize: msg.deckSize,
        });
        setError(null);
        break;
      case "game_over":
        setGameOver({
          winner: msg.winner,
          winnerName: msg.winnerName,
          finalStandings: msg.finalStandings,
        });
        break;
      case "error":
        setError(msg.message);
        break;
    }
  }, []);

  const username =
    typeof window !== "undefined"
      ? localStorage.getItem("coup_username") ?? "anonymous"
      : "anonymous";

  const { status, send } = useWebSocket({
    path: `/ws/game/${code}`,
    onMessage,
  });

  // Join the game room on connect
  const joinedRef = useRef(false);
  useEffect(() => {
    if (status === "connected" && !joinedRef.current) {
      send({ type: "join", username, code });
      joinedRef.current = true;
    }
  }, [status, send, username, code]);

  const handleAction = useCallback(
    (action: number) => {
      send({ type: "action", action });
    },
    [send]
  );

  // ── Game over screen ──────────────────────────────────────────

  if (gameOver) {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center p-4">
        <div className="border border-border-term p-8 max-w-md w-full text-center">
          <h1 className="text-you text-3xl mb-4">GAME OVER</h1>
          <div className="text-cursor text-2xl mb-6">
            {gameOver.winnerName} wins!
          </div>

          <div className="border border-border-term mb-6">
            <div className="border-b border-border-term p-2 text-text-dim text-xs">
              {"// FINAL STANDINGS"}
            </div>
            <div className="divide-y divide-border-term">
              {gameOver.finalStandings.map((s) => (
                <div
                  key={s.seat}
                  className="flex items-center justify-between p-3"
                >
                  <span className={s.alive ? "text-you" : "text-dead"}>
                    {s.alive ? "●" : "☠"}
                  </span>
                  <span
                    className={
                      s.seat === gameOver.winner
                        ? "text-you"
                        : "text-text-default"
                    }
                  >
                    {s.name}
                  </span>
                </div>
              ))}
            </div>
          </div>

          <div className="space-y-2">
            <button
              onClick={() => router.push("/")}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors"
            >
              {">> PLAY AGAIN <<"}
            </button>
          </div>
        </div>
      </div>
    );
  }

  // ── Connection / loading states ───────────────────────────────

  if (!gameState) {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center p-4">
        <StatusIndicator status={status} error={error} />
        <div className="text-text-dim mt-4">
          {status === "connected"
            ? "Waiting for game state..."
            : "Connecting to game server..."}
        </div>
      </div>
    );
  }

  // ── Main game board ───────────────────────────────────────────

  return (
    <div className="relative">
      {/* Connection status overlay */}
      {status !== "connected" && (
        <div className="fixed top-0 left-0 right-0 z-40 bg-bg/90 border-b border-coin-loss p-2 text-center">
          <StatusIndicator status={status} error={error} />
        </div>
      )}

      {/* Error banner */}
      {error && (
        <div className="fixed top-0 left-0 right-0 z-40 bg-bg border-b border-dead p-2 text-center text-dead text-sm">
          ERROR: {error}
        </div>
      )}

      <GameBoard state={gameState} onAction={handleAction} />
    </div>
  );
}

function StatusIndicator({
  status,
  error,
}: {
  status: ConnectionStatus;
  error: string | null;
}) {
  const dotColor =
    status === "connected"
      ? "bg-coin-gain"
      : status === "reconnecting" || status === "connecting"
        ? "bg-cursor"
        : "bg-dead";

  return (
    <div className="flex items-center gap-2 text-sm">
      <span className={`w-2 h-2 rounded-full ${dotColor} ${status === "reconnecting" || status === "connecting" ? "animate-pulse" : ""}`} />
      <span className="text-text-dim">
        {status === "connected"
          ? "Connected"
          : status === "connecting"
            ? "Connecting..."
            : status === "reconnecting"
              ? "Reconnecting..."
              : "Disconnected"}
      </span>
      {error && <span className="text-dead text-xs ml-2">{error}</span>}
    </div>
  );
}
