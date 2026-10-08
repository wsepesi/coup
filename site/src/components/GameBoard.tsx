"use client";

import { useState, useCallback } from "react";
import type { GameState } from "@/lib/types";
import { actionTarget } from "@/lib/constants";
import PlayerCard from "./PlayerCard";
import HandDisplay from "./HandDisplay";
import ActionPicker from "./ActionPicker";
import ExchangePicker from "./ExchangePicker";
import HistoryLog from "./HistoryLog";
import PhaseIndicator from "./PhaseIndicator";
import DeckTracker from "./DeckTracker";
import { useGameTracker } from "@/hooks/useGameTracker";

interface GameBoardProps {
  state: GameState;
  /** True while an action we sent hasn't been answered yet. */
  busy: boolean;
  onAction: (action: number, then?: number) => void;
  onQuit: () => void;
  onRules: () => void;
}

export default function GameBoard({ state, busy, onAction, onQuit, onRules }: GameBoardProps) {
  const { you, cards, players, active, turnPlayer, actions, history, prompt, deck, phase } = state;
  const spectating = you < 0;
  const me = spectating ? null : players[you];
  const isYourTurn = !spectating && active === you && actions.length > 0;
  const exchanging = isYourTurn && phase === "exchange_discard" && !!state.drawn;

  const { lastActions, deckEvents } = useGameTracker(history, players);
  const [hoverTarget, setHoverTarget] = useState<number | null>(null);
  const handleTargetHover = useCallback((seat: number | null) => setHoverTarget(seat), []);

  // Target of the action being resolved (shown on the board for everyone).
  const pendingTarget = state.pending != null ? actionTarget(state.pending) : null;
  const target = hoverTarget ?? pendingTarget;

  // Opponents in seat order starting after you, so the table reads clockwise.
  const n = players.length;
  const start = spectating ? 0 : you + 1;
  const opponents = Array.from({ length: spectating ? n : n - 1 }, (_, k) => (start + k) % n);

  return (
    <div className="h-dvh flex flex-col p-2 sm:p-3 max-w-3xl mx-auto gap-1.5 sm:gap-2 overflow-hidden">
      <div className="flex items-center justify-between text-xs text-text-dim shrink-0">
        <span>
          room <span className="text-text-default tracking-widest">{state.code}</span>
          {spectating && <span className="ml-2 text-cursor">· spectating</span>}
        </span>
        <span className="flex gap-2">
          <button onClick={onRules} className="border border-border-term px-2 py-0.5 hover:text-text-default" aria-label="Rules">? rules</button>
          <button onClick={onQuit} className="border border-border-term px-2 py-0.5 hover:text-text-default">
            {spectating ? "leave" : "quit"} <span className="hidden sm:inline">(Q)</span>
          </button>
        </span>
      </div>

      <div
        className="grid gap-1 sm:gap-2 shrink-0"
        style={{ gridTemplateColumns: `repeat(${Math.min(opponents.length, 3)}, minmax(0, 1fr))` }}
      >
        {opponents.map((seat) => (
          <PlayerCard
            key={seat}
            player={players[seat]}
            isActive={seat === active}
            isTurn={seat === turnPlayer}
            isTarget={target === seat}
            isBlocker={state.block?.seat === seat}
            lastAction={lastActions.get(seat)}
          />
        ))}
      </div>

      <div className="shrink-0">
        <PhaseIndicator prompt={prompt} isYourTurn={isYourTurn} deadline={state.deadline} deck={deck} turn={phase === "action" ? state.turn + 1 : Math.max(1, state.turn)} />
      </div>

      <div className="flex gap-1 sm:gap-2 flex-1 min-h-0">
        <div className="hidden sm:block w-44 shrink-0">
          <DeckTracker deckSize={deck} events={deckEvents} players={players} yourCards={cards} />
        </div>
        <div className="flex-1 min-w-0">
          <HistoryLog entries={history} />
        </div>
      </div>

      {me && (
        <div className={`shrink-0 ${target === you ? "ring-1 ring-cursor" : ""} ${!me.alive ? "opacity-60" : ""}`}>
          <div className="text-center text-xs mb-0.5">
            <span className={active === you ? "text-you font-bold" : "text-text-dim"}>
              {me.name} (you){!me.alive && " — eliminated, watching"}
            </span>
          </div>
          {!exchanging && (
            <HandDisplay
              cards={cards}
              coins={me.coins}
              claims={me.claims}
              selectable={isYourTurn && phase === "lose_card" && !busy}
              onSelect={(slot) => onAction(28 + slot)}
            />
          )}
        </div>
      )}

      {isYourTurn && (
        <div className="shrink-0">
          {exchanging ? (
            <ExchangePicker cards={cards} drawn={state.drawn!} disabled={busy} onConfirm={([a, b]) => onAction(a, b)} />
          ) : (
            <ActionPicker
              actions={actions}
              onAction={(a) => onAction(a)}
              players={players}
              phase={phase}
              cards={cards}
              coins={me?.coins ?? 0}
              disabled={busy}
              onTargetHover={handleTargetHover}
            />
          )}
        </div>
      )}
    </div>
  );
}
