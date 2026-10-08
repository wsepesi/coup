"use client";

import { useState, useEffect, useCallback } from "react";
import { useRouter } from "next/navigation";
import { useKeyboard } from "@/hooks/useKeyboard";
import { createRoom, getSavedName, saveName, parseRoomCode } from "@/lib/identity";
import RulesContent from "@/components/Rules";

const ASCII_TITLE = `
 ██████  ██████  ██    ██ ██████
██      ██    ██ ██    ██ ██   ██
██      ██    ██ ██    ██ ██████
██      ██    ██ ██    ██ ██
 ██████  ██████   ██████  ██
`;

type Mode = "main" | "bots" | "join" | "rules";
const DIFFICULTIES = ["easy", "medium", "hard"] as const;
const MENU_ITEMS = 3;

export default function LandingPage() {
  const router = useRouter();
  const [username, setUsername] = useState("");
  const [mode, setMode] = useState<Mode>("main");
  const [menuIndex, setMenuIndex] = useState(0);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [botPlayers, setBotPlayers] = useState(4);
  const [botDifficulty, setBotDifficulty] = useState<(typeof DIFFICULTIES)[number]>("medium");
  const [joinCode, setJoinCode] = useState("");

  useEffect(() => {
    setUsername(getSavedName());
    try {
      const p = Number(localStorage.getItem("coup_bot_players"));
      if (p >= 2 && p <= 6) setBotPlayers(p);
      const d = localStorage.getItem("coup_bot_difficulty");
      if (d === "easy" || d === "medium" || d === "hard") setBotDifficulty(d);
    } catch { /* storage unavailable */ }
  }, []);

  const name = username.trim();

  const onNameChange = (v: string) => {
    setUsername(v);
    saveName(v.trim());
  };

  const go = useCallback(async (opts: Parameters<typeof createRoom>[0]) => {
    if (!name || busy) return;
    setBusy(true);
    setError(null);
    try {
      const code = await createRoom(opts);
      router.push(`/lobby/${code}`);
    } catch (e) {
      setError((e as Error).message);
      setBusy(false);
    }
  }, [name, busy, router]);

  const handlePlayBots = useCallback(() => {
    try {
      localStorage.setItem("coup_bot_players", String(botPlayers));
      localStorage.setItem("coup_bot_difficulty", botDifficulty);
    } catch { /* ignore */ }
    void go({ bots: Array(botPlayers - 1).fill(botDifficulty), autoStart: true });
  }, [go, botPlayers, botDifficulty]);

  const handleCreate = useCallback(() => void go({}), [go]);

  const parsedCode = parseRoomCode(joinCode);
  const handleJoin = useCallback(() => {
    if (!name || !parsedCode) return;
    router.push(`/lobby/${parsedCode}`);
  }, [name, parsedCode, router]);

  const menuActions = useCallback((index: number) => {
    if (!name) return;
    setError(null);
    switch (index) {
      case 0: setMode("bots"); break;
      case 1: handleCreate(); break;
      case 2: setMode("join"); break;
    }
  }, [name, handleCreate]);

  useKeyboard(
    {
      ArrowUp: () => setMenuIndex((i) => (i > 0 ? i - 1 : MENU_ITEMS - 1)),
      ArrowDown: () => setMenuIndex((i) => (i < MENU_ITEMS - 1 ? i + 1 : 0)),
      Enter: () => menuActions(menuIndex),
      "1": () => menuActions(0),
      "2": () => menuActions(1),
      "3": () => menuActions(2),
      r: () => setMode("rules"),
      R: () => setMode("rules"),
    },
    [menuIndex, menuActions],
    mode === "main",
  );

  useKeyboard(
    {
      Escape: () => setMode("main"),
      ArrowLeft: () => setBotPlayers((p) => Math.max(2, p - 1)),
      ArrowRight: () => setBotPlayers((p) => Math.min(6, p + 1)),
      Enter: handlePlayBots,
    },
    [handlePlayBots],
    mode === "bots",
  );

  useKeyboard({ Escape: () => setMode("main"), Enter: handleJoin }, [handleJoin], mode === "join");
  useKeyboard({ Escape: () => setMode("main"), r: () => setMode("main"), R: () => setMode("main") }, [], mode === "rules");

  return (
    <main className={`min-h-dvh flex flex-col items-center p-4 ${mode === "rules" ? "justify-start pt-4" : "justify-center"}`}>
      {mode !== "rules" && (
        <>
          <pre aria-label="Coup" className="text-you text-[0.55rem] sm:text-sm md:text-base leading-tight select-none mb-2">
            {ASCII_TITLE}
          </pre>
          <p className="text-text-dim text-sm mb-8">{"// the card game of bluffing & deception"}</p>

          <div className="w-full max-w-md mb-6">
            <label className="flex items-center gap-2 border border-border-term p-3 bg-bg focus-within:border-cursor">
              <span className="text-text-dim" aria-hidden>{">"}</span>
              <span className="text-text-dim">name:</span>
              <input
                type="text"
                value={username}
                onChange={(e) => onNameChange(e.target.value)}
                onKeyDown={(e) => { if (e.key === "Enter" && name) (e.target as HTMLInputElement).blur(); }}
                placeholder="type your name"
                maxLength={20}
                autoComplete="nickname"
                aria-label="Your name"
                className="flex-1 min-w-0 bg-transparent outline-none text-you placeholder:text-text-dim caret-cursor"
                autoFocus={!getSavedName()}
              />
            </label>
            {!name && <p className="text-text-dim text-xs mt-1">Enter a name to play.</p>}
          </div>
        </>
      )}

      {error && (
        <div role="alert" className="w-full max-w-md border border-dead text-dead p-2 mb-4 text-sm text-center">{error}</div>
      )}

      {mode === "main" && (
        <div className="w-full max-w-md space-y-3">
          <MenuButton label="[1] Play vs Bots" desc="solo" onClick={() => menuActions(0)} disabled={!name || busy} selected={menuIndex === 0} />
          <MenuButton label="[2] Play with Friends" desc={busy ? "creating room…" : "create a room"} onClick={() => menuActions(1)} disabled={!name || busy} selected={menuIndex === 1} />
          <MenuButton label="[3] Join Game" desc="have a code?" onClick={() => menuActions(2)} disabled={!name || busy} selected={menuIndex === 2} />
        </div>
      )}

      {mode === "bots" && (
        <div className="w-full max-w-md space-y-4">
          <BackButton onClick={() => setMode("main")} />
          <div className="border border-border-term p-4 space-y-4">
            <h2 className="text-text-bright text-lg">Play vs Bots</h2>
            <div className="flex items-center justify-between">
              <span className="text-text-dim" id="players-label">Players:</span>
              <div className="flex items-center gap-1" role="group" aria-labelledby="players-label">
                <button aria-label="Fewer players" onClick={() => setBotPlayers(Math.max(2, botPlayers - 1))} className="text-text-dim hover:text-text-bright px-3 py-1">◀</button>
                <span className="text-you w-4 text-center" aria-live="polite">{botPlayers}</span>
                <button aria-label="More players" onClick={() => setBotPlayers(Math.min(6, botPlayers + 1))} className="text-text-dim hover:text-text-bright px-3 py-1">▶</button>
              </div>
            </div>
            <div className="flex items-center justify-between">
              <span className="text-text-dim">Bots:</span>
              <div className="flex gap-2" role="radiogroup" aria-label="Bot difficulty">
                {DIFFICULTIES.map((d) => (
                  <button
                    key={d}
                    role="radio"
                    aria-checked={botDifficulty === d}
                    onClick={() => setBotDifficulty(d)}
                    className={`px-2 py-1 border ${botDifficulty === d ? "border-you text-you" : "border-border-term text-text-dim hover:text-text-default"}`}
                  >
                    {d}
                  </button>
                ))}
              </div>
            </div>
            <div className="text-text-dim text-xs">
              You vs {botPlayers - 1} {botDifficulty} bot{botPlayers > 2 ? "s" : ""}
              <span className="ml-2 hidden sm:inline">{"// ◀▶ players  Enter start"}</span>
            </div>
            <button
              onClick={handlePlayBots}
              disabled={busy}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-50"
            >
              {busy ? "starting…" : ">> PLAY <<"}
            </button>
          </div>
        </div>
      )}

      {mode === "join" && (
        <div className="w-full max-w-md space-y-4">
          <BackButton onClick={() => setMode("main")} />
          <form
            className="border border-border-term p-4 space-y-4"
            onSubmit={(e) => { e.preventDefault(); handleJoin(); }}
          >
            <h2 className="text-text-bright text-lg">Join Game</h2>
            <label className="flex items-center gap-2 border border-border-term p-3 focus-within:border-cursor">
              <span className="text-text-dim">code:</span>
              <input
                type="text"
                value={joinCode}
                onChange={(e) => setJoinCode(e.target.value)}
                placeholder="ABCDE"
                aria-label="Room code or link"
                autoCapitalize="characters"
                autoComplete="off"
                spellCheck={false}
                className="flex-1 min-w-0 bg-transparent outline-none text-you placeholder:text-text-dim uppercase tracking-widest text-center text-xl"
                autoFocus
              />
            </label>
            <p className="text-text-dim text-xs">Paste the code or the invite link your friend sent.</p>
            <button
              type="submit"
              disabled={!parsedCode || !name}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-30 disabled:cursor-not-allowed"
            >
              {">> JOIN <<"}
            </button>
          </form>
        </div>
      )}

      {mode === "rules" && (
        <div className="w-full max-w-5xl space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"} <span className="text-text-dim text-xs">(ESC / R)</span>
          </button>
          <div className="border border-border-term p-4 sm:p-6">
            <RulesContent />
          </div>
        </div>
      )}

      {mode !== "rules" && (
        <div className="mt-12 text-text-dim text-xs text-center">
          <p>{"// bots are heuristic — easy, medium (honest), hard (bluffs)"}</p>
          {mode === "main" && (
            <p className="mt-2">
              <button onClick={() => setMode("rules")} className="hover:text-text-default transition-colors underline-offset-2 hover:underline">
                {"(R) how to play"}
              </button>
            </p>
          )}
        </div>
      )}
    </main>
  );
}

function BackButton({ onClick }: { onClick: () => void }) {
  return (
    <button onClick={onClick} className="text-text-dim hover:text-text-default text-sm">
      {"< back"} <span className="text-text-dim text-xs hidden sm:inline">(ESC)</span>
    </button>
  );
}

function MenuButton({ label, desc, onClick, disabled, selected }: {
  label: string; desc?: string; onClick: () => void; disabled?: boolean; selected?: boolean;
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className={`w-full text-left border p-3 transition-colors disabled:opacity-30 disabled:cursor-not-allowed group ${
        selected ? "border-cursor text-cursor bg-selection-bg/40" : "border-border-term hover:border-cursor hover:text-cursor"
      }`}
    >
      <span>
        <span className={`mr-1 ${selected ? "" : "opacity-0"}`} aria-hidden>▸</span>
        {label}
      </span>
      {desc && <span className="text-text-dim text-sm ml-2">{"// " + desc}</span>}
    </button>
  );
}
