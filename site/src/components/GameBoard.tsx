"use client";

import { useState, useCallback } from "react";
import type { GameState } from "@/lib/types";
import PlayerCard from "./PlayerCard";
import HandDisplay from "./HandDisplay";
import ActionPicker from "./ActionPicker";
import ExchangePicker from "./ExchangePicker";
import HistoryLog from "./HistoryLog";
import PhaseIndicator from "./PhaseIndicator";
import DeckTracker from "./DeckTracker";
import { useGameTracker, getClaimDisplay } from "@/hooks/useGameTracker";

interface GameBoardProps {
  state: GameState;
  onAction: (action: number) => void;
  onExchangeConfirm?: (discardActions: [number, number]) => void;
  onQuit?: () => void;
}

export default function GameBoard({ state, onAction, onExchangeConfirm, onQuit }: GameBoardProps) {
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
  const isExchange = state.phase === "exchange_discard" && yourCards.length > 2;

  // Track last actions, deck events from history (claims come from server)
  const { lastActions, deckEvents } = useGameTracker(history, players);

  // Convert server claims (Record<number, string[]>) to Map<number, Set<string>>
  const serverClaims = state.claims ?? {};
  const claimsMap = new Map<number, Set<string>>(
    Object.entries(serverClaims).map(([k, v]) => [Number(k), new Set(v)])
  );

  // Arc offsets: center opponents at top (0 margin), edges pushed down
  const arcOffsets = opponents.map((_, i) => {
    const n = opponents.length;
    if (n <= 2) return 0;
    const center = (n - 1) / 2;
    const distFromCenter = Math.abs(i - center) / center; // 0 at center, 1 at edges
    return Math.round(distFromCenter * distFromCenter * 20); // quadratic curve, max 20px
  });

  // Active player is a bot and it's not your turn = thinking
  const activeIsBot = !isYourTurn && players[activePlayer]?.isBot;

  // Target seat for arrow indicators
  const [targetSeat, setTargetSeat] = useState<number | null>(null);
  const handleTargetHover = useCallback((seat: number | null) => setTargetSeat(seat), []);

  return (
    <div className="h-dvh flex flex-col p-2 sm:p-3 max-w-3xl mx-auto gap-1 sm:gap-2 overflow-hidden">
      {/* Quit button — mobile tap target, desktop has Q key */}
      {onQuit && (
        <button
          onClick={onQuit}
          className="sm:hidden absolute top-2 right-2 z-10 text-text-dim text-xs border border-border-term px-2 py-0.5"
        >
          ✕ quit
        </button>
      )}

      {/* Opponents in half-arc */}
      <div className="flex flex-nowrap gap-1 sm:gap-2 justify-center items-start shrink-0">
        {opponents.map((p, i) => (
          <div key={p.seat} className="flex-1 min-w-0 max-w-[160px]" style={{ marginTop: `${arcOffsets[i]}px` }}>
            <PlayerCard
              player={p}
              seat={p.seat}
              isActive={p.seat === activePlayer}
              isTarget={targetSeat === p.seat}
              claims={getClaimDisplay(claimsMap.get(p.seat))}
              lastAction={lastActions.get(p.seat)}
              isThinking={activeIsBot && p.seat === activePlayer}
            />
          </div>
        ))}
      </div>

      {/* Center phase display */}
      <div className="shrink-0">
        <PhaseIndicator
          context={context}
          activePlayer={activePlayer}
          players={players}
          isYourTurn={isYourTurn}
          deckSize={deckSize}
        />
      </div>

      {/* Deck tracker + History — share the flex-grow space */}
      <div className="flex gap-1 sm:gap-2 flex-1 min-h-0">
        <div className="hidden sm:block w-48 shrink-0">
          <DeckTracker deckSize={deckSize} events={deckEvents} />
        </div>
        <div className="flex-1 min-w-0">
          <HistoryLog entries={history} />
        </div>
      </div>

      {/* Your hand */}
      <div className="shrink-0">
        <div className="text-center text-text-dim text-xs">
          {"★"} YOU {"★"}
        </div>
        <HandDisplay
          cards={yourCards}
          coins={you?.coins ?? 0}
          isExchange={isExchange}
          ownClaims={getClaimDisplay(claimsMap.get(yourSeat))}
        />
      </div>

      {/* Actions */}
      {isYourTurn && availableActions.length > 0 && (
        <div className="shrink-0">
          {isExchange && onExchangeConfirm ? (
            <ExchangePicker yourCards={yourCards} onConfirm={onExchangeConfirm} />
          ) : (
            <>
              <ActionPicker actions={availableActions} onAction={onAction} players={players} phase={state.phase} yourCards={yourCards} onTargetHover={handleTargetHover} />
              <div className="text-center text-text-dim text-xs mt-1">
                Arrow keys + Enter | 1-9 quick select | (H) history
              </div>
            </>
          )}
        </div>
      )}

      {/* Waiting */}
      {!isYourTurn && (
        <div className="text-center text-text-dim text-sm shrink-0">
          {activeIsBot
            ? `${players[activePlayer]?.name ?? "opponent"} is thinking...`
            : `Waiting for ${players[activePlayer]?.name ?? "opponent"}...`}
        </div>
      )}
    </div>
  );
}
