"use client";

import { useState, useCallback } from "react";
import type { CardInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ACTION } from "@/lib/constants";
import { useKeyboard } from "@/hooks/useKeyboard";

interface ExchangePickerProps {
  cards: CardInfo[]; // your hand (slots 0,1)
  drawn: number[]; // drawn cards (slots 2,3)
  disabled?: boolean;
  onConfirm: (discardActions: [number, number]) => void;
}

/** Pick which cards to keep; the rest (always two) go back to the deck. */
export default function ExchangePicker({ cards, drawn, disabled, onConfirm }: ExchangePickerProps) {
  // Selectable slots: your living cards + both drawn cards. Dead cards stay dead.
  const options = [
    ...cards.map((c, i) => ({ slot: i, type: c.type, alive: c.alive, source: "yours" as const })).filter((o) => o.alive),
    ...drawn.map((t, i) => ({ slot: 2 + i, type: t, alive: true, source: "drawn" as const })),
  ];
  const keepCount = options.length - 2;
  const [keep, setKeep] = useState<Set<number>>(() => new Set(options.filter((o) => o.source === "yours").map((o) => o.slot)));
  const [cursor, setCursor] = useState(0);

  const toggle = useCallback((slot: number) => {
    setKeep((prev) => {
      const next = new Set(prev);
      if (next.has(slot)) next.delete(slot);
      else if (next.size < keepCount) next.add(slot);
      else if (keepCount === 1) return new Set([slot]); // single keep: just switch
      return next;
    });
  }, [keepCount]);

  const ready = keep.size === keepCount;
  const confirm = useCallback(() => {
    if (!ready || disabled) return;
    const discard = options.map((o) => o.slot).filter((s) => !keep.has(s)).sort((a, b) => a - b);
    onConfirm([ACTION.DISCARD0 + discard[0], ACTION.DISCARD0 + discard[1]]);
  }, [ready, disabled, options, keep, onConfirm]);

  useKeyboard(
    {
      ArrowLeft: () => setCursor((c) => Math.max(0, c - 1)),
      ArrowRight: () => setCursor((c) => Math.min(options.length - 1, c + 1)),
      " ": () => options[cursor] && toggle(options[cursor].slot),
      Enter: confirm,
      ...Object.fromEntries(options.map((o, i) => [String(i + 1), () => toggle(o.slot)])),
    },
    [cursor, toggle, confirm, options.length],
  );

  return (
    <div className="border-t border-border-term pt-2">
      <div className="text-text-bright text-sm mb-2 text-center">
        Keep {keepCount} card{keepCount > 1 ? "s" : ""} — {ready ? "ready" : `${keepCount - keep.size} more to pick`}
      </div>
      <div className="flex gap-2 justify-center flex-wrap" role="group" aria-label="Cards to keep">
        {options.map((o, i) => {
          const name = ROLE_NAMES[o.type] ?? "???";
          const on = keep.has(o.slot);
          return (
            <button
              key={o.slot}
              onClick={() => { setCursor(i); toggle(o.slot); }}
              aria-pressed={on}
              className={`border-2 px-3 py-2 text-sm min-w-[6.5rem] transition-all ${on ? "bg-selection-bg/40" : "opacity-60 border-dashed"} ${cursor === i ? "ring-1 ring-cursor" : ""}`}
              style={{ borderColor: ROLE_COLORS[name], color: ROLE_COLORS[name] }}
            >
              <div className="font-bold">{on ? "✓ " : ""}{name}</div>
              <div className="text-[0.7rem] text-text-dim">{i + 1} · {o.source === "yours" ? "your card" : "drawn"}</div>
            </button>
          );
        })}
      </div>
      <button
        onClick={confirm}
        disabled={!ready || disabled}
        className={`w-full mt-2 py-2 border text-sm transition-colors ${ready && !disabled ? "border-you text-you hover:bg-you/10" : "border-border-term text-text-dim opacity-40"}`}
      >
        {disabled ? "…" : ">> KEEP SELECTED <<"} <span className="text-xs opacity-60 hidden sm:inline">(Enter)</span>
      </button>
    </div>
  );
}
