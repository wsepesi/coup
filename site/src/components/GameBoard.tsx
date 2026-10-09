"use client";

import { useMemo } from "react";
import type { GameState } from "@/lib/types";
import { actionTarget } from "@/lib/constants";
import Table from "./Table";
import ActionPicker from "./ActionPicker";
import ExchangePicker from "./ExchangePicker";
import HistoryPanel from "./HistoryPanel";
import PhaseIndicator from "./PhaseIndicator";

interface GameBoardProps {
  state: GameState;
  /** True while an action we sent hasn't been answered yet. */
  busy: boolean;
  onAction: (action: number, then?: number) => void;
  onQuit: () => void;
  onRules: () => void;
}

// Layout: the table and your controls on the left, the full history on the
// right (stacked on narrow screens). The screen shows what you'd see in person
// plus a complete written record of the game; it does no analysis for you.
export default function GameBoard({ state, busy, onAction, onQuit, onRules }: GameBoardProps) {
  const { you, cards, players, active, turnPlayer, actions, history, prompt, deck, phase } = state;
  const spectating = you < 0;
  const me = spectating ? null : players[you];
  const isYourTurn = !spectating && active === you && actions.length > 0;
  const exchanging = isYourTurn && phase === "exchange_discard" && !!state.drawn;
  const turn = phase === "action" ? state.turn + 1 : Math.max(1, state.turn);

  const target = state.pending != null && phase !== "action" ? actionTarget(state.pending) : null;
  const turnEntries = useMemo(
    () => (phase === "action" ? [] : history.filter((e) => e.turn === state.turn)),
    [history, phase, state.turn],
  );
  const seats = useMemo(
    () => players.map((p) => ({ name: p.name, coins: p.coins, influence: p.influence, alive: p.alive })),
    [players],
  );

  return (
    <div className="min-h-dvh lg:h-dvh flex flex-col p-2 sm:p-3 gap-2 max-w-[1800px] mx-auto">
      <div className="flex items-center justify-between text-xs text-text-dim shrink-0 border-b border-border-term pb-1.5">
        <span className="flex items-baseline gap-4">
          <span className="text-text-bright font-bold tracking-[0.2em] text-sm">COUP</span>
          <span>room <span className="text-text-default tracking-widest">{state.code}</span></span>
          {spectating && <span className="text-cursor">spectating</span>}
          {me && !me.alive && <span className="text-cursor">you're out — watching</span>}
        </span>
        <span className="flex gap-2">
          <button onClick={onRules} className="border border-border-term px-2 py-0.5 hover:text-text-default" aria-label="Rules">? rules</button>
          <button onClick={onQuit} className="border border-border-term px-2 py-0.5 hover:text-text-default">
            {spectating ? "leave" : "quit"} <span className="hidden sm:inline">(Q)</span>
          </button>
        </span>
      </div>

      <div className="flex-1 min-h-0 flex flex-col lg:flex-row gap-3">
        <div className="lg:w-1/2 lg:min-w-0 flex flex-col gap-2 min-h-0">
          <div className="h-[22rem] sm:h-[26rem] lg:h-auto lg:flex-1 lg:min-h-0">
            <Table
              players={players}
              you={you}
              cards={cards}
              turnPlayer={turnPlayer}
              active={active}
              target={target}
              deck={deck}
              turn={turn}
              turnEntries={turnEntries}
            />
          </div>
          <div className="shrink-0">
            <PhaseIndicator prompt={prompt} isYourTurn={isYourTurn} deadline={state.deadline} deck={deck} turn={turn} />
          </div>
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
                />
              )}
            </div>
          )}
        </div>

        <div className="lg:w-1/2 lg:min-w-0 h-[70vh] lg:h-auto min-h-0">
          <HistoryPanel entries={history} seats={seats} you={you} />
        </div>
      </div>
    </div>
  );
}
