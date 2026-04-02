"use client";

import { useState, useCallback, useRef, useEffect } from "react";
import { useParams, useSearchParams, useRouter } from "next/navigation";
import { useWebSocket } from "@/lib/ws";
import { useKeyboard } from "@/hooks/useKeyboard";
import type { ServerMessage, LobbyPlayer, ClientMessage, HouseRules } from "@/lib/types";

export default function LobbyPage() {
  const params = useParams();
  const searchParams = useSearchParams();
  const router = useRouter();
  const rawCode = params.code as string;

  const [roomCode, setRoomCode] = useState<string>(
    rawCode === "new" ? "" : rawCode
  );
  const [players, setPlayers] = useState<LobbyPlayer[]>([]);
  const [houseRules, setHouseRules] = useState<HouseRules>({ refundOnChallenge: true });
  const [error, setError] = useState<string | null>(null);
  const [isHost, setIsHost] = useState(false);
  const [phase, setPhase] = useState<"matchmaker" | "game">(
    rawCode === "new" ? "matchmaker" : "game"
  );
  // Store all search params in state/refs so they survive the URL rewrite from matchmaker
  const [username] = useState(() =>
    searchParams.get("username")
    ?? (typeof window !== "undefined" ? localStorage.getItem("coup_username") : null)
    ?? "anonymous"
  );
  const [action] = useState(() => searchParams.get("action"));
  const initParams = useRef({
    numPlayers: Number(searchParams.get("numPlayers") ?? 6),
    numBots: Number(searchParams.get("numBots") ?? 0),
    botDifficulty: (searchParams.get("botDifficulty") ?? "medium") as "easy" | "medium",
  });

  // Timeout for matchmaker phase
  const [matchmakerTimedOut, setMatchmakerTimedOut] = useState(false);
  useEffect(() => {
    if (phase !== "matchmaker") return;
    const timer = setTimeout(() => setMatchmakerTimedOut(true), 8000);
    return () => clearTimeout(timer);
  }, [phase]);

  // Matchmaker connection
  const onMatchmakerMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "room_created":
        setRoomCode(msg.code);
        setIsHost(true);
        window.history.replaceState(null, "", `/lobby/${msg.code}`);
        setPhase("game");
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
    onConnect: useCallback((send: (msg: ClientMessage) => void) => {
      if (action === "create") {
        send({
          type: "create",
          username,
          ...initParams.current,
        });
      }
    }, [action, username]),
  });

  // Game room connection
  const onGameMessage = useCallback((msg: ServerMessage) => {
    switch (msg.type) {
      case "lobby":
        setPlayers(msg.players);
        if (msg.houseRules) setHouseRules(msg.houseRules);
        if (msg.isHost != null) setIsHost(msg.isHost);
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
  const gameJoined = useRef(false);
  const game = useWebSocket({
    path: `/ws/game/${gameWsCode}`,
    enabled: phase === "game" && !!gameWsCode,
    onMessage: onGameMessage,
    onConnect: useCallback((send: (msg: ClientMessage) => void) => {
      send({ type: "join", username, code: gameWsCode });
      gameJoined.current = true;
    }, [username, gameWsCode]),
  });


  const handleStart = useCallback(() => game.send({ type: "start" }), [game]);
  const copyCode = () => { if (roomCode) navigator.clipboard.writeText(roomCode); };

  // Keyboard: Enter to start, Escape to leave
  useKeyboard(
    {
      Enter: () => { if (isHost && players.length >= 1) handleStart(); },
      Escape: () => router.push("/"),
    },
    [isHost, players.length, handleStart, router],
  );

  const humanCount = Math.max(1, players.filter(p => !p.isBot).length);
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
        ) : matchmakerTimedOut ? (
          <div className="space-y-2">
            <span className="text-dead">Failed to create room.</span>
            <div className="flex gap-3 justify-center">
              <button
                onClick={() => { setMatchmakerTimedOut(false); window.location.reload(); }}
                className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5"
              >
                retry
              </button>
              <button
                onClick={() => router.push("/")}
                className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5"
              >
                back
              </button>
            </div>
          </div>
        ) : matchmaker.status === "reconnecting" ? (
          <span className="text-text-dim">Connection lost. Retrying...</span>
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

      {/* House Rules */}
      <div className="w-full max-w-md border border-border-term mb-4">
        <div className="border-b border-border-term px-3 py-1 text-text-dim text-xs">
          {"// "}house rules
        </div>
        <div className="px-3 py-2">
          <label className="flex items-center justify-between text-sm">
            <span className="text-text-default">Refund coins on challenge</span>
            {isHost ? (
              <button
                onClick={() => {
                  const updated = { ...houseRules, refundOnChallenge: !houseRules.refundOnChallenge };
                  setHouseRules(updated);
                  game.send({ type: "house_rules", houseRules: updated });
                }}
                className={`px-2 py-0.5 text-xs border transition-colors ${
                  houseRules.refundOnChallenge
                    ? "border-you text-you"
                    : "border-border-term text-text-dim"
                }`}
              >
                {houseRules.refundOnChallenge ? "ON" : "OFF"}
              </button>
            ) : (
              <span className={`text-xs ${houseRules.refundOnChallenge ? "text-you" : "text-text-dim"}`}>
                {houseRules.refundOnChallenge ? "ON" : "OFF"}
              </span>
            )}
          </label>
          <p className="text-text-dim text-xs mt-1">
            Refund coins when action claim is successfully challenged (official rule)
          </p>
        </div>
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
          {"< leave"} <span className="text-text-dim text-xs">(ESC)</span>
        </button>
      </div>
    </div>
  );
}
