"use client";

import type { GameState } from "@/lib/types";
import PlayerCard from "./PlayerCard";
import HandDisplay from "./HandDisplay";
import ActionPicker from "./ActionPicker";
import HistoryLog from "./HistoryLog";
import PhaseIndicator from "./PhaseIndicator";

interface GameBoardProps {
  state: GameState;
  onAction: (action: number) => void;
}

export default function GameBoard({ state, onAction }: GameBoardProps) {
  const {
    yourSeat,
    yourCards,
    players,
    activePlayer,
    isYourTurn,
    availableActions,
    history,
    context,
    deckSize,
  } = state;

  const opponents = players
    .map((p, i) => ({ ...p, seat: i }))
    .filter((p) => p.seat !== yourSeat);

  const you = players[yourSeat];

  return (
    <div className="min-h-screen flex flex-col p-4 max-w-3xl mx-auto gap-3">
      {/* Opponents */}
      <div className="flex flex-wrap gap-2 justify-center">
        {opponents.map((p) => (
          <PlayerCard
            key={p.seat}
            player={p}
            seat={p.seat}
            isActive={p.seat === activePlayer}
          />
        ))}
      </div>

      {/* Center phase display */}
      <PhaseIndicator
        context={context}
        activePlayer={activePlayer}
        players={players}
        isYourTurn={isYourTurn}
        deckSize={deckSize}
      />

      {/* History */}
      <HistoryLog entries={history} />

      {/* Your hand */}
      <HandDisplay cards={yourCards} coins={you?.coins ?? 0} />

      {/* Actions */}
      {isYourTurn && availableActions.length > 0 && (
        <ActionPicker actions={availableActions} onAction={onAction} />
      )}

      {/* Waiting */}
      {!isYourTurn && (
        <div className="text-center text-text-dim text-sm">
          Waiting for {players[activePlayer]?.name ?? "opponent"}...
        </div>
      )}
    </div>
  );
}
