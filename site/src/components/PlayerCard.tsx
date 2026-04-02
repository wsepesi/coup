"use client";

import type { PlayerInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SHORT, HIDDEN_CARD } from "@/lib/constants";

interface ClaimDisplay {
  short: string;
  color: string;
}

interface PlayerCardProps {
  player: PlayerInfo;
  seat: number;
  isActive: boolean;
  isTarget?: boolean;
  claims?: ClaimDisplay[];
  lastAction?: string;
  isThinking?: boolean;
}

const SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

export default function PlayerCard({ player, seat, isActive, isTarget, claims, lastAction, isThinking }: PlayerCardProps) {
  const influence = player.influence;
  const indicator =
    influence === 2 ? "●" : influence === 1 ? "○" : "☠";
  const isDead = !player.alive;

  return (
    <div className="flex flex-col items-center gap-0.5">
      {/* Target arrow above */}
      <div className={`text-cursor text-xs h-4 transition-opacity ${isTarget ? "opacity-100" : "opacity-0"}`}>
        ▼
      </div>

      <div
        className={`font-mono text-xs sm:text-sm leading-snug min-w-0 w-full px-2 sm:px-3 py-1 sm:py-2 border transition-colors ${
          isTarget
            ? "border-cursor bg-cursor/5"
            : isActive
              ? "border-cursor"
              : "border-border-term"
        } ${isDead ? "opacity-40" : ""}`}
      >
        {/* Line 1: P# Name ● */}
        <div className="flex items-center gap-1">
          {isActive && <span className="text-cursor">▶</span>}
          <span className={isActive ? "text-text-bright" : isDead ? "text-text-dim" : "text-text-default"}>
            P{seat} {player.name}
          </span>
          <span className={isDead ? "text-dead" : "text-text-dim"}>{indicator}</span>
          {isThinking && (
            <span className="text-text-dim animate-spin-slow">{SPINNER_FRAMES[0]}</span>
          )}
        </div>

        {/* Active turn bar */}
        {isActive && !isDead && (
          <div className="h-0.5 bg-cursor mt-0.5 rounded-full" />
        )}

        {/* Line 2: ▓▓ [As] 5● */}
        <div className="flex items-center gap-1 mt-0.5">
          {Array.from({ length: influence }).map((_, i) => (
            <span key={`h-${i}`} className="text-text-dim">{HIDDEN_CARD}</span>
          ))}
          {player.revealed.map((card, i) => {
            const name = ROLE_NAMES[card.type] ?? "?";
            const short = ROLE_SHORT[name] ?? "??";
            const color = ROLE_COLORS[name] ?? "#CC0000";
            return (
              <span key={`r-${i}`} style={{ color }}>[{short}]</span>
            );
          })}
          <span className="text-text-default ml-1">{player.coins}●</span>
        </div>

        {/* Line 3: Claimed roles (color-coded abbreviations) */}
        {claims && claims.length > 0 && !isDead && (
          <div className="flex items-center gap-1 mt-0.5">
            {claims.map((c, i) => (
              <span key={i} className="text-xs" style={{ color: c.color }}>{c.short}</span>
            ))}
          </div>
        )}

        {/* Line 4: Last action */}
        {lastAction && !isDead && (
          <div className="text-text-dim text-xs mt-0.5 truncate">
            → {lastAction}
          </div>
        )}
      </div>

      {/* Target arrow below */}
      <div className={`text-cursor text-xs h-4 transition-opacity ${isTarget ? "opacity-100" : "opacity-0"}`}>
        ▲
      </div>
    </div>
  );
}
