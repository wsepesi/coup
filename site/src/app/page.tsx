"use client";

import { useState, useEffect } from "react";
import { useRouter } from "next/navigation";

const ASCII_TITLE = `
 ██████  ██████  ██    ██ ██████
██      ██    ██ ██    ██ ██   ██
██      ██    ██ ██    ██ ██████
██      ██    ██ ██    ██ ██
 ██████  ██████   ██████  ██
`;

type Mode = "main" | "bots" | "join" | "ai";
const DIFFICULTIES = ["easy", "medium", "hard"] as const;

export default function LandingPage() {
  const router = useRouter();
  const [username, setUsername] = useState("");
  const [mode, setMode] = useState<Mode>("main");

  // Bot game config
  const [botPlayers, setBotPlayers] = useState(4);
  const [botDifficulty, setBotDifficulty] = useState<string>("medium");

  // Join
  const [joinCode, setJoinCode] = useState("");

  useEffect(() => {
    const saved = localStorage.getItem("coup_username");
    if (saved) setUsername(saved);
  }, []);

  const saveUsername = (name: string) => {
    setUsername(name);
    localStorage.setItem("coup_username", name);
  };

  const handlePlayBots = () => {
    if (!username.trim()) return;
    const params = new URLSearchParams({
      action: "create",
      username: username.trim(),
      numPlayers: String(botPlayers),
      numBots: String(botPlayers - 1),
      botDifficulty,
      autoStart: "true",
    });
    router.push(`/lobby/new?${params.toString()}`);
  };

  const handleCreate = () => {
    if (!username.trim()) return;
    // Create with max seats, host configures in lobby
    const params = new URLSearchParams({
      action: "create",
      username: username.trim(),
      numPlayers: "6",
      numBots: "0",
      botDifficulty: "medium",
    });
    router.push(`/lobby/new?${params.toString()}`);
  };

  const handleJoin = () => {
    if (!username.trim() || !joinCode.trim()) return;
    const params = new URLSearchParams({
      action: "join",
      username: username.trim(),
    });
    router.push(`/lobby/${joinCode.trim().toUpperCase()}?${params.toString()}`);
  };

  return (
    <div className="min-h-screen flex flex-col items-center justify-center p-4">
      <pre className="text-you text-xs sm:text-sm md:text-base leading-tight select-none mb-2">
        {ASCII_TITLE}
      </pre>
      <p className="text-text-dim text-sm mb-8">
        {"// the card game of bluff & deception"}
      </p>

      {/* Username */}
      <div className="w-full max-w-md mb-8">
        <div className="flex items-center gap-2 border border-border-term p-3 bg-bg">
          <span className="text-text-dim">{">"}</span>
          <span className="text-text-dim">username:</span>
          <input
            type="text"
            value={username}
            onChange={(e) => saveUsername(e.target.value)}
            placeholder="anonymous"
            maxLength={20}
            className="flex-1 bg-transparent outline-none text-you placeholder:text-text-dim caret-cursor"
            autoFocus
          />
          {!username && <span className="text-cursor animate-pulse">_</span>}
        </div>
      </div>

      {/* Main menu */}
      {mode === "main" && (
        <div className="w-full max-w-md space-y-3">
          <MenuButton
            label="[1] Play vs Bots"
            desc="solo"
            onClick={() => setMode("bots")}
            disabled={!username.trim()}
          />
          <MenuButton
            label="[2] Create Game"
            desc="multiplayer"
            onClick={handleCreate}
            disabled={!username.trim()}
          />
          <MenuButton
            label="[3] Join Game"
            desc="multiplayer"
            onClick={() => setMode("join")}
            disabled={!username.trim()}
          />
          <MenuButton
            label="[4] Play vs AI"
            desc="offline, coming soon"
            onClick={() => setMode("ai")}
            disabled={!username.trim()}
          />
        </div>
      )}

      {/* Play vs Bots */}
      {mode === "bots" && (
        <div className="w-full max-w-md space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"}
          </button>
          <div className="border border-border-term p-4 space-y-4">
            <h2 className="text-text-bright text-lg">Play vs Bots</h2>

            <div className="flex items-center justify-between">
              <span className="text-text-dim">Players:</span>
              <div className="flex items-center gap-2">
                <button onClick={() => setBotPlayers(Math.max(2, botPlayers - 1))} className="text-text-dim hover:text-text-bright px-2">◀</button>
                <span className="text-you w-4 text-center">{botPlayers}</span>
                <button onClick={() => setBotPlayers(Math.min(6, botPlayers + 1))} className="text-text-dim hover:text-text-bright px-2">▶</button>
              </div>
            </div>

            <div className="flex items-center justify-between">
              <span className="text-text-dim">Difficulty:</span>
              <div className="flex gap-2">
                {DIFFICULTIES.map((d) => (
                  <button
                    key={d}
                    onClick={() => setBotDifficulty(d)}
                    className={`px-2 py-0.5 border ${
                      botDifficulty === d
                        ? "border-you text-you"
                        : "border-border-term text-text-dim hover:text-text-default"
                    }`}
                  >
                    {d}
                  </button>
                ))}
              </div>
            </div>

            <div className="text-text-dim text-xs">
              You vs {botPlayers - 1} {botDifficulty} bot{botPlayers > 2 ? "s" : ""}
            </div>

            <button
              onClick={handlePlayBots}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors"
            >
              {">> PLAY <<"}
            </button>
          </div>
        </div>
      )}

      {/* Join game */}
      {mode === "join" && (
        <div className="w-full max-w-md space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"}
          </button>
          <div className="border border-border-term p-4 space-y-4">
            <h2 className="text-text-bright text-lg">Join Game</h2>
            <div className="flex items-center gap-2 border border-border-term p-3">
              <span className="text-text-dim">code:</span>
              <input
                type="text"
                value={joinCode}
                onChange={(e) => setJoinCode(e.target.value.toUpperCase())}
                placeholder="ABCD"
                maxLength={8}
                className="flex-1 bg-transparent outline-none text-you placeholder:text-text-dim uppercase tracking-widest text-center text-xl"
                autoFocus
              />
            </div>
            <button
              onClick={handleJoin}
              disabled={!joinCode.trim()}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-30 disabled:cursor-not-allowed"
            >
              {">> JOIN <<"}
            </button>
          </div>
        </div>
      )}

      {/* AI mode */}
      {mode === "ai" && (
        <div className="w-full max-w-md space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"}
          </button>
          <div className="border border-border-term p-4 text-center">
            <p className="text-text-dim mb-2">{"// PLAY VS AI"}</p>
            <p className="text-cursor text-lg">Coming soon</p>
            <p className="text-text-dim text-sm mt-2">
              Offline mode with trained neural network policy.
            </p>
          </div>
        </div>
      )}

      <div className="mt-12 text-text-dim text-xs text-center">
        <p>{"// coup v0.1 // terminal edition"}</p>
      </div>
    </div>
  );
}

function MenuButton({ label, desc, onClick, disabled }: {
  label: string; desc?: string; onClick: () => void; disabled?: boolean;
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className="w-full text-left border border-border-term p-3 hover:border-cursor hover:text-cursor transition-colors disabled:opacity-30 disabled:cursor-not-allowed group"
    >
      <span className="group-hover:text-cursor">{label}</span>
      {desc && <span className="text-text-dim text-sm ml-2">{"// " + desc}</span>}
    </button>
  );
}
