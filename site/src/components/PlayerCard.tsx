"use client";

import type { PlayerInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SHORT, HIDDEN_CARD } from "@/lib/constants";

interface PlayerCardProps {
  player: PlayerInfo;
  isActive: boolean;
  isTurn: boolean;
  isTarget?: boolean;
  isBlocker?: boolean;
  lastAction?: string;
  onClick?: () => void;
}

export default function PlayerCard({ player, isActive, isTurn, isTarget, isBlocker, lastAction, onClick }: PlayerCardProps) {
  const isDead = !player.alive;
  const status = player.bot ? "bot" : player.away ? "away" : !player.online ? "offline" : null;
  const border = isTarget ? "border-cursor bg-cursor/5" : isActive ? "border-you" : isTurn ? "border-cursor/60" : "border-border-term";

  return (
    <div className="flex flex-col items-center gap-0.5 min-w-0">
      <div aria-hidden className={`text-cursor text-xs h-3 leading-3 transition-opacity ${isTarget ? "opacity-100" : "opacity-0"}`}>▼</div>
      <div
        onClick={onClick}
        aria-label={`${player.name}${isDead ? ", eliminated" : `, ${player.coins} coins, ${player.influence} influence`}${isActive ? ", deciding" : ""}`}
        className={`font-mono text-xs sm:text-sm leading-snug min-w-0 w-full px-2 sm:px-3 py-1 sm:py-1.5 border transition-colors ${border} ${isDead ? "opacity-40" : ""} ${isActive ? "shadow-[0_0_0_1px_var(--you)]" : ""}`}
      >
        <div className="flex items-center gap-1 min-w-0">
          {isTurn && !isDead && <span className="text-cursor shrink-0" aria-hidden>▶</span>}
          <span className={`truncate ${isActive ? "text-text-bright font-bold" : isDead ? "text-text-dim line-through" : "text-text-default"}`} title={player.name}>
            {player.name}
          </span>
          {isActive && !isDead && <span className="thinking-dots text-you shrink-0" aria-hidden />}
        </div>

        <div className="flex items-center gap-1 mt-0.5 flex-wrap">
          {Array.from({ length: player.influence }).map((_, i) => (
            <span key={`h-${i}`} className="text-text-dim" aria-hidden>{HIDDEN_CARD}</span>
          ))}
          {player.revealed.map((type, i) => {
            const name = ROLE_NAMES[type] ?? "?";
            return (
              <span key={`r-${i}`} className="line-through decoration-1" style={{ color: ROLE_COLORS[name] }} title={`${name} (lost)`}>
                {ROLE_SHORT[name]}
              </span>
            );
          })}
          <span className="text-text-default ml-auto tabular-nums" title={`${player.coins} coins`}>{player.coins}●</span>
        </div>

        <div className="flex items-center gap-1 mt-0.5 min-h-[1rem] text-[0.7rem] sm:text-xs">
          {!isDead && player.claims.map((c) => (
            <span key={c} style={{ color: ROLE_COLORS[c] }} title={`Has claimed ${c}`}>{ROLE_SHORT[c]}</span>
          ))}
          {isBlocker && <span className="text-cursor" title="Blocking">⛨</span>}
          {status && <span className={`ml-auto ${status === "bot" ? "text-text-dim" : "text-coin-loss"}`}>{status}</span>}
        </div>

        <div className="text-text-dim text-[0.7rem] sm:text-xs truncate hidden sm:block min-h-[1rem]" title={lastAction}>
          {lastAction && !isDead ? `→ ${lastAction}` : ""}
        </div>
      </div>
    </div>
  );
}
