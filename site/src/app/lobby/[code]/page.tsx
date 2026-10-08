"use client";

import { useState, useCallback, useEffect, useRef } from "react";
import { useParams, useRouter } from "next/navigation";
import { useRoom } from "@/lib/ws";
import { getSavedName, saveName } from "@/lib/identity";
import { useKeyboard } from "@/hooks/useKeyboard";
import GameBoard from "@/components/GameBoard";
import Lobby from "@/components/Lobby";
import GameOver from "@/components/GameOver";
import RulesContent from "@/components/Rules";

/**
 * The room page: one WebSocket for the whole lobby → game → game-over → rematch
 * cycle. (/game/[code] redirects here.)
 */
export default function RoomPage() {
  const params = useParams<{ code: string }>();
  const code = (params.code ?? "").toUpperCase();
  const [name, setName] = useState<string | null>(null);
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    setName(getSavedName() || null);
    setLoaded(true);
  }, []);

  if (!loaded) return <Centered>Loading…</Centered>;
  if (!name) return <NamePrompt code={code} onSubmit={(n) => { saveName(n); setName(n); }} />;
  return <Room code={code} name={name} />;
}

function Room({ code, name }: { code: string; name: string }) {
  const router = useRouter();
  const conn = useRoom(code, name);
  const { game, lobby, result, status, send } = conn;
  const [confirmQuit, setConfirmQuit] = useState(false);
  const [showRules, setShowRules] = useState(false);
  const [sentStep, setSentStep] = useState<number | null>(null);

  // Clear the "waiting for server" lock once the decision moves on (or on error).
  useEffect(() => { setSentStep(null); }, [game, conn.error]);

  // Hold the final board for a moment so players see the winning blow before the results.
  const lastGame = useRef(game);
  if (game) lastGame.current = game;
  const [holdBoard, setHoldBoard] = useState(false);
  useEffect(() => {
    if (!result) return;
    setHoldBoard(true);
    const t = setTimeout(() => setHoldBoard(false), 2200);
    return () => clearTimeout(t);
  }, [result]);
  const boardGame = game ?? (result && holdBoard ? lastGame.current : null);

  const busy = sentStep != null && game?.step === sentStep;
  const handleAction = useCallback((action: number, then?: number) => {
    if (!game || busy) return;
    setSentStep(game.step);
    send({ type: "action", action, then, step: game.step });
  }, [game, busy, send]);

  const leave = useCallback(() => {
    send({ type: "leave" });
    router.push("/");
  }, [send, router]);

  // Tab title: flag when it's your move (handy with the tab in the background).
  const yourMove = !!game && game.you >= 0 && game.active === game.you && game.actions.length > 0;
  useEffect(() => {
    document.title = yourMove ? "▶ Your move — COUP" : result ? "Game over — COUP" : game ? `COUP · ${code}` : `Lobby ${code} — COUP`;
  }, [yourMove, result, game, code]);

  useKeyboard({ q: () => setConfirmQuit(true), "?": () => setShowRules((v) => !v) }, [], !!game && !confirmQuit && !showRules);
  useKeyboard(
    { y: () => { setConfirmQuit(false); leave(); }, n: () => setConfirmQuit(false), Escape: () => setConfirmQuit(false) },
    [leave],
    confirmQuit,
  );
  useKeyboard({ Escape: () => setShowRules(false), "?": () => setShowRules(false) }, [], showRules);

  if (conn.closedReason) {
    const replaced = /another tab/.test(conn.closedReason);
    return (
      <Centered>
        <p className="text-text-bright mb-4">{conn.closedReason}</p>
        <div className="flex gap-2 justify-center">
          {replaced && (
            <button onClick={conn.reconnect} className="border border-you text-you px-4 py-2 hover:bg-you/10">use this tab</button>
          )}
          <button onClick={() => router.push("/")} className="border border-border-term px-4 py-2 text-text-dim hover:text-text-default">main menu</button>
        </div>
      </Centered>
    );
  }

  let body: React.ReactNode;
  if (result && !boardGame) {
    body = (
      <GameOver
        result={result}
        lobby={lobby}
        youName={lobby?.seats.find((s) => s.you)?.name ?? name}
        onRematch={() => send({ type: "start" })}
        onLobby={conn.dismissResult}
        onHome={leave}
      />
    );
  } else if (boardGame) {
    body = (
      <GameBoard
        state={game ? boardGame : { ...boardGame, actions: [], deadline: null, prompt: `${result?.winnerName ?? "Someone"} wins!` }}
        busy={busy}
        onAction={handleAction}
        onQuit={() => setConfirmQuit(true)}
        onRules={() => setShowRules(true)}
      />
    );
  } else if (lobby) {
    body = <Lobby lobby={lobby} send={send} onLeave={leave} />;
  } else {
    body = (
      <Centered>
        <ConnectionDot status={status} />
        <p className="text-text-dim mt-3">{status === "reconnecting" ? "Can't reach the server — retrying…" : `Joining room ${code}…`}</p>
      </Centered>
    );
  }

  return (
    <div className="relative">
      {conn.outdated && (
        <Banner tone="info">
          A new version is available. <button className="underline" onClick={() => window.location.reload()}>Refresh</button>
        </Banner>
      )}
      {status === "reconnecting" && (lobby || game) && (
        <Banner tone="warn"><ConnectionDot status={status} /> Connection lost — reconnecting…</Banner>
      )}
      {conn.error && (
        <Banner tone="error">
          {conn.error}
          <button onClick={conn.clearError} className="ml-3 text-xs underline" aria-label="Dismiss">dismiss</button>
        </Banner>
      )}

      {body}

      {confirmQuit && game && (
        <div className="fixed inset-0 z-50 bg-bg/90 flex items-center justify-center p-4" role="dialog" aria-modal="true" aria-label="Leave game">
          <div className="border border-border-term p-6 max-w-sm w-full text-center bg-bg">
            <div className="text-text-bright text-lg mb-2">{game.you >= 0 && game.players[game.you]?.alive ? "Leave the game?" : "Leave the room?"}</div>
            <div className="text-text-dim text-sm mb-4">
              {game.you >= 0 && game.players[game.you]?.alive
                ? "A bot will play your cards for the rest of this game. The others can keep playing."
                : "You can come back with the room link."}
            </div>
            <div className="flex gap-2 justify-center">
              <button onClick={() => { setConfirmQuit(false); leave(); }} className="border border-dead text-dead px-4 py-2 text-sm">
                Leave <span className="text-xs opacity-60">(Y)</span>
              </button>
              <button autoFocus onClick={() => setConfirmQuit(false)} className="border border-border-term text-text-dim px-4 py-2 text-sm">
                Stay <span className="text-xs opacity-60">(N)</span>
              </button>
            </div>
          </div>
        </div>
      )}

      {showRules && (
        <div className="fixed inset-0 z-50 bg-bg overflow-y-auto p-4" role="dialog" aria-label="Rules">
          <div className="max-w-5xl mx-auto">
            <button onClick={() => setShowRules(false)} className="text-text-dim hover:text-text-default text-sm mb-3 border border-border-term px-2 py-0.5">
              ✕ close <span className="text-xs hidden sm:inline">(Esc)</span>
            </button>
            <RulesContent />
          </div>
        </div>
      )}
    </div>
  );
}

function NamePrompt({ code, onSubmit }: { code: string; onSubmit: (name: string) => void }) {
  const [value, setValue] = useState("");
  const ref = useRef<HTMLInputElement>(null);
  const trimmed = value.trim();
  return (
    <Centered>
      <form
        className="w-full max-w-sm border border-border-term p-4 space-y-3 text-left"
        onSubmit={(e) => { e.preventDefault(); if (trimmed) onSubmit(trimmed); }}
      >
        <h1 className="text-you text-lg">Join room {code}</h1>
        <label className="flex items-center gap-2 border border-border-term p-3 focus-within:border-cursor">
          <span className="text-text-dim">name:</span>
          <input
            ref={ref}
            autoFocus
            value={value}
            onChange={(e) => setValue(e.target.value)}
            maxLength={20}
            autoComplete="nickname"
            aria-label="Your name"
            placeholder="type your name"
            className="flex-1 min-w-0 bg-transparent outline-none text-you placeholder:text-text-dim"
          />
        </label>
        <button type="submit" disabled={!trimmed} className="w-full border border-you text-you py-2 hover:bg-you/10 disabled:opacity-30">
          {">> JOIN <<"}
        </button>
      </form>
    </Centered>
  );
}

function Centered({ children }: { children: React.ReactNode }) {
  return <main className="min-h-dvh flex flex-col items-center justify-center p-4 text-center">{children}</main>;
}

function Banner({ tone, children }: { tone: "info" | "warn" | "error"; children: React.ReactNode }) {
  const cls = tone === "error" ? "border-dead text-dead" : tone === "warn" ? "border-coin-loss text-text-default" : "border-you text-text-default";
  return (
    <div role={tone === "error" ? "alert" : "status"} className={`fixed top-0 inset-x-0 z-40 bg-bg border-b ${cls} px-3 py-1.5 text-center text-sm flex items-center justify-center gap-2`}>
      {children}
    </div>
  );
}

function ConnectionDot({ status }: { status: string }) {
  const color = status === "connected" ? "bg-coin-gain" : status === "closed" ? "bg-dead" : "bg-cursor animate-pulse";
  return <span className={`inline-block w-2 h-2 rounded-full ${color}`} aria-hidden />;
}
