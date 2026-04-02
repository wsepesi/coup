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
    <div className="border border-border-term px-4 py-3 text-center">
      <div className="flex items-center justify-between text-xs text-text-dim mb-1">
        <span>Deck ({deckSize})</span>
        <span>{isYourTurn ? "YOUR TURN" : `${activePlayerName}'s turn`}</span>
      </div>
      {context && (
        <div className="text-text-bright text-sm">{context}</div>
      )}
      {isYourTurn && !context && (
        <div className="text-cursor text-sm animate-pulse">
          Choose your action...
        </div>
      )}
    </div>
  );
}
