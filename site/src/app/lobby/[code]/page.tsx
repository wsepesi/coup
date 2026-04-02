"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import { useParams, useSearchParams, useRouter } from "next/navigation";
import { useWebSocket } from "@/lib/ws";
import type { ServerMessage, LobbyPlayer } from "@/lib/types";

export default function LobbyPage() {
  const params = useParams();
  const searchParams = useSearchParams();
  const router = useRouter();
  const rawCode = params.code as string;

  const [roomCode, setRoomCode] = useState<string>(
    rawCode === "new" ? "" : rawCode
  );
  const [players, setPlayers] = useState<LobbyPlayer[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [isHost, setIsHost] = useState(false);
  const [phase, setPhase] = useState<"matchmaker" | "game">(
    rawCode === "new" ? "matchmaker" : "game"
  );
  const [joined, setJoined] = useState(false);

  const username = searchParams.get("username") ?? "anonymous";
  const action = searchParams.get("action");
  const autoStart = searchParams.get("autoStart") === "true";

  // Matchmaker connection
  const onMatchmakerMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "room_created":
        setRoomCode(msg.code);
        setIsHost(true);
        window.history.replaceState(null, "", `/lobby/${msg.code}`);
        setPhase("game");
        setJoined(false);
        break;
      case "error":
        setError(msg.message);
        break;
    }
  }, []);

  const matchmaker = useWebSocket({
    path: "/ws/matchmaker",
    enabled: phase === "matchmaker",
    onMessage: onMatchmakerMessage,
  });

  useEffect(() => {
    if (phase !== "matchmaker" || matchmaker.status !== "connected" || joined) return;
    if (action === "create") {
      matchmaker.send({
        type: "create",
        username,
        numPlayers: Number(searchParams.get("numPlayers") ?? 6),
        numBots: Number(searchParams.get("numBots") ?? 0),
        botDifficulty: (searchParams.get("botDifficulty") ?? "medium") as "easy" | "medium",
      });
    }
    setJoined(true);
  }, [phase, matchmaker.status, joined, action, username, searchParams, matchmaker.send]);

  // Game room connection
  const onGameMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "lobby":
        setPlayers(msg.players);
        break;
      case "state":
        router.push(`/game/${roomCode || rawCode}`);
        break;
      case "error":
        setError(msg.message);
        break;
    }
  }, [roomCode, rawCode, router]);

  const gameWsCode = roomCode || (rawCode !== "new" ? rawCode : "");
  const game = useWebSocket({
    path: `/ws/game/${gameWsCode}`,
    enabled: phase === "game" && !!gameWsCode,
    onMessage: onGameMessage,
  });

  const gameJoined = useRef(false);
  useEffect(() => {
    if (phase !== "game" || game.status !== "connected" || gameJoined.current) return;
    game.send({ type: "join", username, code: gameWsCode });
    gameJoined.current = true;
  }, [phase, game.status, username, gameWsCode, game.send]);

  const numBots = Number(searchParams.get("numBots") ?? 0);
  const isBotGame = numBots > 0 && autoStart;

  // Auto-start for bot games once we've joined
  const autoStarted = useRef(false);
  useEffect(() => {
    if (autoStart && isHost && game.status === "connected" && gameJoined.current && !autoStarted.current) {
      // Small delay to ensure join message is processed
      const timer = setTimeout(() => {
        game.send({ type: "start" });
        autoStarted.current = true;
      }, 300);
      return () => clearTimeout(timer);
    }
  }, [autoStart, isHost, game.status, game.send]);

  const handleStart = () => game.send({ type: "start" });
  const copyCode = () => { if (roomCode) navigator.clipboard.writeText(roomCode); };

  const humanCount = players.filter(p => !p.isBot).length;
  const botCount = players.filter(p => p.isBot).length;

  return (
    <div className="min-h-screen flex flex-col items-center justify-center p-4">
      <div className="text-center mb-6">
        <h1 className="text-you text-xl mb-2">LOBBY</h1>
        {roomCode ? (
          <div className="flex items-center gap-3 justify-center">
            <span className="text-text-dim">Room:</span>
            <span className="text-cursor text-2xl tracking-[0.3em] font-bold">{roomCode}</span>
            <button onClick={copyCode} className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5">
              copy
            </button>
          </div>
        ) : (
          <span className="text-text-dim">Creating room...</span>
        )}
      </div>

      {error && (
        <div className="border border-dead text-dead p-2 mb-4 max-w-md w-full text-center text-sm">
          {error}
        </div>
      )}

      {/* Player list */}
      <div className="w-full max-w-md border border-border-term mb-4">
        <div className="border-b border-border-term px-3 py-1 text-text-dim text-xs">
          {"// "}{humanCount} human{humanCount !== 1 ? "s" : ""}{botCount > 0 && ` + ${botCount} bot${botCount !== 1 ? "s" : ""}`}
        </div>
        {players.length === 0 ? (
          <div className="p-3 text-text-dim text-sm text-center">
            Waiting for players...
          </div>
        ) : (
          <div className="divide-y divide-border-term">
            {players.map((p) => (
              <div key={p.seat} className="flex items-center justify-between px-3 py-2 text-sm">
                <div className="flex items-center gap-2">
                  <span className="text-text-dim w-5">P{p.seat}</span>
                  <span className={p.username === username ? "text-you" : "text-text-default"}>
                    {p.username}
                    {p.username === username && <span className="text-xs ml-1">(you)</span>}
                  </span>
                </div>
                {p.isBot && (
                  <span className="text-text-dim text-xs">bot</span>
                )}
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Host controls */}
      <div className="w-full max-w-md space-y-2">
        {isHost && (
          <button
            onClick={handleStart}
            disabled={players.length < 1}
            className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {">> START GAME <<"}
          </button>
        )}
        <button
          onClick={() => router.push("/")}
          className="w-full text-text-dim hover:text-text-default text-sm"
        >
          {"< leave"}
        </button>
      </div>
    </div>
  );
}
