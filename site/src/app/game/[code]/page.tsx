"use client";

import { useState, useCallback, useRef, useEffect } from "react";
import { useParams, useRouter } from "next/navigation";
import { useWebSocket, type ConnectionStatus } from "@/lib/ws";
import { useKeyboard } from "@/hooks/useKeyboard";
import type { ServerMessage, GameState, Standing, HistoryEntry } from "@/lib/types";
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
    totalTurns: number;
    history: HistoryEntry[];
  } | null>(null);
  const [showPostGameHistory, setShowPostGameHistory] = useState(false);
  const [showQuitConfirm, setShowQuitConfirm] = useState(false);
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
          claims: msg.claims ?? {},
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
          totalTurns: msg.totalTurns,
          history: msg.history,
        });
        break;
      case "error":
        setError(msg.message);
        break;
      case "forfeited":
        // Game was forfeited — bounce back to lobby
        router.push(`/lobby/${code}`);
        break;
    }
  }, [router, code]);

  const username =
    typeof window !== "undefined"
      ? localStorage.getItem("coup_username") ?? "anonymous"
      : "anonymous";

  const { status, send } = useWebSocket({
    path: `/ws/game/${code}`,
    onMessage,
    onConnect: useCallback((sendMsg: (msg: import("@/lib/types").ClientMessage) => void) => {
      sendMsg({ type: "join", username, code });
    }, [username, code]),
  });

  // Queue for second exchange discard action
  const pendingExchangeRef = useRef<number | null>(null);

  // Auto-send pending exchange discard when server asks for the second one
  useEffect(() => {
    if (
      gameState?.phase === "exchange_discard" &&
      gameState.isYourTurn &&
      pendingExchangeRef.current != null
    ) {
      const action = pendingExchangeRef.current;
      pendingExchangeRef.current = null;
      send({ type: "action", action });
    }
  }, [gameState, send]);

  const handleAction = useCallback(
    (action: number) => {
      send({ type: "action", action });
    },
    [send]
  );

  const handleExchangeConfirm = useCallback(
    (discardActions: [number, number]) => {
      // Send first discard immediately, queue second
      pendingExchangeRef.current = discardActions[1];
      send({ type: "action", action: discardActions[0] });
    },
    [send]
  );

  const [historyCopied, setHistoryCopied] = useState(false);

  const copyHistory = useCallback(() => {
    if (!gameOver) return;
    const text = gameOver.history.map((h) => `[T${h.turn}] ${h.text}`).join("\n");
    navigator.clipboard.writeText(text).then(() => {
      setHistoryCopied(true);
      setTimeout(() => setHistoryCopied(false), 2000);
    });
  }, [gameOver]);

  // Quit shortcut (Q) during gameplay
  useKeyboard(
    {
      q: () => setShowQuitConfirm(true),
      Q: () => setShowQuitConfirm(true),
    },
    [],
    !!gameState && !gameOver && !showQuitConfirm,
  );

  // Quit confirm shortcuts
  useKeyboard(
    {
      y: () => { send({ type: "forfeit" }); setShowQuitConfirm(false); },
      Y: () => { send({ type: "forfeit" }); setShowQuitConfirm(false); },
      n: () => setShowQuitConfirm(false),
      N: () => setShowQuitConfirm(false),
      Escape: () => setShowQuitConfirm(false),
    },
    [send],
    showQuitConfirm,
  );

  // Game over keyboard shortcuts
  useKeyboard(
    {
      Enter: () => router.push(`/lobby/${code}`),
      l: () => router.push(`/lobby/${code}`),
      L: () => router.push(`/lobby/${code}`),
      m: () => router.push("/"),
      M: () => router.push("/"),
      h: () => setShowPostGameHistory((h) => !h),
      H: () => setShowPostGameHistory((h) => !h),
      c: copyHistory,
      C: copyHistory,
    },
    [router, code, copyHistory],
    !!gameOver,
  );

  // ── Game over screen ──────────────────────────────────────────

  if (gameOver) {
    return (
      <div className="min-h-screen flex items-center justify-center p-4">
        <div className="border border-border-term p-8 max-w-md w-full text-center">
          <h1 className="text-you text-3xl mb-4">GAME OVER</h1>
          <div className="text-cursor text-2xl mb-6">
            {gameOver.winnerName} wins!
          </div>

          <div className="border border-border-term mb-4">
            <div className="border-b border-border-term p-2 text-text-dim text-xs">
              {"// FINAL STANDINGS"}
            </div>
            <div className="divide-y divide-border-term">
              {gameOver.finalStandings.map((s, i) => {
                const ordinals = ["1st", "2nd", "3rd", "4th", "5th", "6th"];
                const ordinal = ordinals[i] ?? `${i + 1}th`;
                const status = s.alive
                  ? "survived"
                  : `eliminated turn ${s.eliminatedTurn}`;
                return (
                  <div
                    key={s.seat}
                    className="flex items-center gap-3 p-2 sm:p-3 text-sm"
                  >
                    <span className="text-text-dim w-8">{ordinal}</span>
                    <span className={s.alive ? "text-you" : "text-dead"}>
                      {s.alive ? "●" : "☠"}
                    </span>
                    <span
                      className={`flex-1 ${
                        s.seat === gameOver.winner
                          ? "text-you"
                          : "text-text-default"
                      }`}
                    >
                      {s.name}
                    </span>
                    <span className="text-text-dim text-xs">{status}</span>
                  </div>
                );
              })}
            </div>
          </div>

          <div className="text-text-dim text-xs mb-4">
            Duration: {gameOver.totalTurns} turns
          </div>

          <div className="space-y-2">
            <button
              onClick={() => router.push(`/lobby/${code}`)}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors"
            >
              {">> BACK TO LOBBY <<"} <span className="text-xs opacity-60">(Enter)</span>
            </button>
            <div className="flex gap-2">
              <button
                onClick={() => router.push("/")}
                className="flex-1 border border-border-term text-text-dim py-2 hover:bg-you/10 transition-colors text-sm"
              >
                {"MAIN MENU"} <span className="text-xs opacity-60">(M)</span>
              </button>
              <button
                onClick={() => setShowPostGameHistory((h) => !h)}
                className={`flex-1 border py-2 hover:bg-you/10 transition-colors text-sm ${
                  showPostGameHistory ? "border-cursor text-cursor" : "border-border-term text-text-dim"
                }`}
              >
                {"HISTORY"} <span className="text-xs opacity-60">(H)</span>
              </button>
            </div>
          </div>
        </div>

        {/* History side panel */}
        {showPostGameHistory && (
          <div className="fixed right-0 top-0 h-full w-80 sm:w-96 border-l border-border-term bg-bg flex flex-col z-50">
            <div className="flex items-center justify-between border-b border-border-term p-3">
              <span className="text-text-dim text-xs">{"// GAME HISTORY"}</span>
              <div className="flex items-center gap-2">
                <button
                  onClick={copyHistory}
                  className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5"
                >
                  {historyCopied ? "copied!" : "copy (C)"}
                </button>
                <button
                  onClick={() => setShowPostGameHistory(false)}
                  className="text-text-dim hover:text-text-default text-xs"
                >
                  ✕
                </button>
              </div>
            </div>
            <div className="flex-1 overflow-y-auto p-3 space-y-1">
              {gameOver.history.map((h, i) => (
                <div key={i} className="text-xs text-text-dim">
                  <span className="opacity-50 mr-2">T{h.turn}</span>
                  {h.text}
                </div>
              ))}
            </div>
          </div>
        )}
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
      {/* Quit confirm overlay */}
      {showQuitConfirm && (
        <div className="fixed inset-0 z-50 bg-bg/90 flex items-center justify-center">
          <div className="border border-border-term p-6 max-w-sm w-full text-center bg-bg">
            <div className="text-text-bright text-lg mb-4">Forfeit game?</div>
            <div className="text-text-dim text-sm mb-4">
              This will end the game for all players.
            </div>
            <div className="flex gap-2 justify-center">
              <button
                onClick={() => { send({ type: "forfeit" }); setShowQuitConfirm(false); }}
                className="border border-dead text-dead px-4 py-2 text-sm transition-colors"
              >
                Yes, forfeit (Y)
              </button>
              <button
                onClick={() => setShowQuitConfirm(false)}
                className="border border-border-term text-text-dim px-4 py-2 text-sm transition-colors"
              >
                Cancel (N)
              </button>
            </div>
          </div>
        </div>
      )}
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

      <GameBoard state={gameState} onAction={handleAction} onExchangeConfirm={handleExchangeConfirm} onQuit={() => setShowQuitConfirm(true)} />
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
