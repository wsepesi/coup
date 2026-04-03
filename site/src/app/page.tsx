"use client";

import { useState, useEffect, useCallback } from "react";
import { useRouter } from "next/navigation";
import { useKeyboard } from "@/hooks/useKeyboard";

const ASCII_TITLE = `
 ██████  ██████  ██    ██ ██████
██      ██    ██ ██    ██ ██   ██
██      ██    ██ ██    ██ ██████
██      ██    ██ ██    ██ ██
 ██████  ██████   ██████  ██
`;

type Mode = "main" | "bots" | "join" | "ai" | "rules";
const DIFFICULTIES = ["easy", "medium", "hard"] as const;
const MENU_ITEMS = 4;

export default function LandingPage() {
  const router = useRouter();
  const [username, setUsername] = useState("");
  const [mode, setMode] = useState<Mode>("main");
  const [menuIndex, setMenuIndex] = useState(0);

  // Bot game config
  const [botPlayers, setBotPlayers] = useState(6);
  const [botDifficulty, setBotDifficulty] = useState<string>("hard");

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

  const handlePlayBots = useCallback(() => {
    if (!username.trim()) return;
    const params = new URLSearchParams({
      action: "create",
      username: username.trim(),
      numPlayers: String(botPlayers),
      numBots: String(botPlayers - 1),
      botDifficulty,
    });
    router.push(`/lobby/new?${params.toString()}`);
  }, [username, botPlayers, botDifficulty, router]);

  const handleCreate = useCallback(() => {
    if (!username.trim()) return;
    const params = new URLSearchParams({
      action: "create",
      username: username.trim(),
      numPlayers: "6",
      numBots: "0",
      botDifficulty: "medium",
    });
    router.push(`/lobby/new?${params.toString()}`);
  }, [username, router]);

  const handleJoin = useCallback(() => {
    if (!username.trim() || !joinCode.trim()) return;
    const params = new URLSearchParams({
      action: "join",
      username: username.trim(),
    });
    router.push(`/lobby/${joinCode.trim().toUpperCase()}?${params.toString()}`);
  }, [username, joinCode, router]);

  const menuActions = useCallback((index: number) => {
    if (!username.trim()) return;
    switch (index) {
      case 0: setMode("bots"); break;
      case 1: handleCreate(); break;
      case 2: setMode("join"); break;
      case 3: setMode("ai"); break;
    }
  }, [username, handleCreate]);

  // Main menu keyboard
  useKeyboard(
    {
      ArrowUp: () => setMenuIndex((i) => (i > 0 ? i - 1 : MENU_ITEMS - 1)),
      ArrowDown: () => setMenuIndex((i) => (i < MENU_ITEMS - 1 ? i + 1 : 0)),
      Enter: () => menuActions(menuIndex),
      "1": () => menuActions(0),
      "2": () => menuActions(1),
      "3": () => menuActions(2),
      "4": () => menuActions(3),
      r: () => setMode("rules"),
      R: () => setMode("rules"),
    },
    [menuIndex, menuActions],
    mode === "main",
  );

  // Bots mode keyboard
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

  // Join mode keyboard
  useKeyboard(
    {
      Escape: () => setMode("main"),
      Enter: handleJoin,
    },
    [handleJoin],
    mode === "join",
  );

  // AI mode keyboard
  useKeyboard(
    { Escape: () => setMode("main") },
    [],
    mode === "ai",
  );

  // Rules mode keyboard
  useKeyboard(
    {
      Escape: () => setMode("main"),
      r: () => setMode("main"),
      R: () => setMode("main"),
    },
    [],
    mode === "rules",
  );

  return (
    <div className={`min-h-screen flex flex-col items-center p-4 ${mode === "rules" ? "justify-start pt-4" : "justify-center"}`}>
      {mode !== "rules" && (
        <>
          <pre className="text-you text-xs sm:text-sm md:text-base leading-tight select-none mb-2">
            {ASCII_TITLE}
          </pre>
          <p className="text-text-dim text-sm mb-8">
            {"// the card game of bluffing & deception"}
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
        </>
      )}

      {/* Main menu */}
      {mode === "main" && (
        <div className="w-full max-w-md space-y-3">
          <MenuButton
            label="[1] Play vs Bots"
            desc="solo"
            onClick={() => menuActions(0)}
            disabled={!username.trim()}
            selected={menuIndex === 0}
          />
          <MenuButton
            label="[2] Create Game"
            desc="multiplayer"
            onClick={() => menuActions(1)}
            disabled={!username.trim()}
            selected={menuIndex === 1}
          />
          <MenuButton
            label="[3] Join Game"
            desc="multiplayer"
            onClick={() => menuActions(2)}
            disabled={!username.trim()}
            selected={menuIndex === 2}
          />
          {/* <MenuButton
            label="[4] Play vs AI"
            desc="offline, coming soon"
            onClick={() => menuActions(3)}
            disabled={!username.trim()}
            selected={menuIndex === 3}
          /> */}
        </div>
      )}

      {/* Play vs Bots */}
      {mode === "bots" && (
        <div className="w-full max-w-md space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"} <span className="text-text-dim text-xs">(ESC)</span>
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
              <span className="ml-2">{"// ◀▶ players  Enter start"}</span>
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
            {"< back"} <span className="text-text-dim text-xs">(ESC)</span>
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
            {"< back"} <span className="text-text-dim text-xs">(ESC)</span>
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

      {/* Rules */}
      {mode === "rules" && (
        <div className="w-full max-w-5xl space-y-4">
          <button onClick={() => setMode("main")} className="text-text-dim hover:text-text-default text-sm">
            {"< back"} <span className="text-text-dim text-xs">(ESC / R)</span>
          </button>
          <div className="border border-border-term p-4 sm:p-6 max-h-[85vh] overflow-y-auto">
            <RulesContent />
          </div>
        </div>
      )}

      {mode !== "rules" && (
        <div className="mt-12 text-text-dim text-xs text-center">
          <p>{"// coup v0.1"}</p>
          <p className="mt-1">{"// bots are heuristic-based // AI coming soon"}</p>
          {mode === "main" && (
            <p className="mt-2">
              <button onClick={() => setMode("rules")} className="hover:text-text-default transition-colors">
                {"(R) rules"}
              </button>
            </p>
          )}
        </div>
      )}
    </div>
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
        selected
          ? "border-cursor text-cursor bg-selection-bg"
          : "border-border-term hover:border-cursor hover:text-cursor"
      }`}
    >
      <span className={selected ? "text-cursor" : "group-hover:text-cursor"}>
        {selected && <span className="mr-1">▸</span>}
        {label}
      </span>
      {desc && <span className="text-text-dim text-sm ml-2">{"// " + desc}</span>}
    </button>
  );
}

const ROLE_STYLES: Record<string, string> = {
  Duke: "#997700",
  Assassin: "#CC0000",
  Captain: "#0055CC",
  Ambassador: "#007733",
  Contessa: "#CC3377",
};

function RoleTag({ name, symbol }: { name: string; symbol: string }) {
  return <span style={{ color: ROLE_STYLES[name] }}>{symbol} {name}</span>;
}

function RulesContent() {
  return (
    <div className="font-mono text-sm">
      {/* Two-column grid on md+, single column on small screens */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-x-8 gap-y-5">
        {/* Left column */}
        <div className="space-y-5">
          <div>
            <h2 className="text-text-bright text-base">OVERVIEW</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">
              Coup is a game of bluffing and deduction for 2-6 players. Each player starts with 2 influence
              cards (face down) and 2 coins. Last player with influence wins.
            </p>
            <p className="text-text-default mt-2">
              You can claim ANY role for your action, even if you don{"'"}t have it — but if someone challenges
              you and you{"'"}re bluffing, you lose a card.
            </p>
          </div>

          <div>
            <h2 className="text-text-bright text-base">THE 5 ROLES</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-3 ml-2">
              <div>
                <RoleTag name="Duke" symbol="♦" />
                <div className="text-text-dim ml-4">Action: Tax — take 3 coins from the treasury</div>
                <div className="text-text-dim ml-4">Blocks: Foreign Aid</div>
              </div>
              <div>
                <RoleTag name="Assassin" symbol="†" />
                <div className="text-text-dim ml-4">Action: Assassinate — pay 3 coins, target loses influence</div>
                <div className="text-text-dim ml-4">Blocked by: Contessa</div>
              </div>
              <div>
                <RoleTag name="Captain" symbol="⚓" />
                <div className="text-text-dim ml-4">Action: Steal — take 2 coins from another player</div>
                <div className="text-text-dim ml-4">Blocks: Stealing</div>
                <div className="text-text-dim ml-4">Blocked by: Captain, Ambassador</div>
              </div>
              <div>
                <RoleTag name="Ambassador" symbol="✦" />
                <div className="text-text-dim ml-4">Action: Exchange — draw 2 cards from the deck, return 2</div>
                <div className="text-text-dim ml-4">Blocks: Stealing</div>
              </div>
              <div>
                <RoleTag name="Contessa" symbol="♥" />
                <div className="text-text-dim ml-4">Action: None</div>
                <div className="text-text-dim ml-4">Blocks: Assassination</div>
              </div>
            </div>
          </div>
        </div>

        {/* Right column */}
        <div className="space-y-5">
          <div>
            <h2 className="text-text-bright text-base">GENERAL ACTIONS</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-2 ml-2 text-text-default">
              <div><span className="text-text-bright">Income</span>{"        "}Take 1 coin. Cannot be blocked or challenged.</div>
              <div><span className="text-text-bright">Foreign Aid</span>{"   "}Take 2 coins. Can be blocked by Duke.</div>
              <div><span className="text-text-bright">Coup</span>{"          "}Pay 7 coins, target loses influence. Mandatory at 10+ coins.</div>
            </div>
          </div>

          <div>
            <h2 className="text-text-bright text-base">CHALLENGES</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">When a player claims a role, any other player may challenge.</p>
            <div className="space-y-1 ml-2 mt-2 text-text-default">
              <div><span className="text-cursor">Bluffing?</span>{"   "}Challenger wins. The bluffer loses an influence card.</div>
              <div><span className="text-cursor">Truthful?</span>{"   "}Challenger loses. Claimant reveals, shuffles back, draws new.</div>
            </div>
          </div>

          <div>
            <h2 className="text-text-bright text-base">BLOCKING</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">
              Some actions can be blocked by specific roles. The blocker claims to have that role.
              The original actor (or anyone) can then challenge the block.
            </p>
          </div>

          <div>
            <h2 className="text-text-bright text-base">STRATEGY TIPS</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-1 ml-2 text-text-dim">
              <div>- Bluffing Duke early is strong — Tax gives 3 coins and Duke blocks Foreign Aid.</div>
              <div>- Challenge more when you hold cards of the claimed type — fewer copies left.</div>
              <div>- At 7+ coins, Coup is often better than role actions — can{"'"}t be blocked.</div>
              <div>- Watch what others claim. If two players both claim Duke, one is likely bluffing.</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
