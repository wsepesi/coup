"use client";

import type { PlayerInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SHORT, HIDDEN_CARD } from "@/lib/constants";

interface PlayerCardProps {
  player: PlayerInfo;
  seat: number;
  isActive: boolean;
}

export default function PlayerCard({ player, seat, isActive }: PlayerCardProps) {
  const influence = player.influence;
  const indicator =
    influence === 2 ? "●" : influence === 1 ? "○" : "☠";
  const isDead = !player.alive;

  return (
    <div
      className={`font-mono text-sm leading-snug min-w-[160px] px-3 py-2 border ${
        isActive
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
      </div>

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
    </div>
  );
}
