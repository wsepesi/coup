"use client";

import type { PlayerInfo } from "@/lib/types";

interface PhaseIndicatorProps {
  context?: string;
  activePlayer: number;
  players: PlayerInfo[];
  isYourTurn: boolean;
  deckSize: number;
}

export default function PhaseIndicator({
  context,
  activePlayer,
  players,
  isYourTurn,
  deckSize,
}: PhaseIndicatorProps) {
  const activePlayerName = players[activePlayer]?.name ?? `Player ${activePlayer}`;

  return (
    <div className="border border-border-term px-2 sm:px-4 py-1.5 sm:py-3 text-center">
      <div className="flex items-center justify-between text-xs text-text-dim mb-0.5 sm:mb-1">
        <span className="sm:hidden">Deck ({deckSize})</span>
        <span>{isYourTurn ? "YOUR TURN" : `${activePlayerName}'s turn`}</span>
      </div>
      {context && (
        <div className="text-text-bright text-xs sm:text-sm">{context}</div>
      )}
      {isYourTurn && !context && (
        <div className="text-cursor text-xs sm:text-sm animate-pulse">
          Choose your action...
        </div>
      )}
    </div>
  );
}
