"use client";

import { useState, useCallback } from "react";
import type { CardInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS } from "@/lib/constants";
import { useKeyboard } from "@/hooks/useKeyboard";

interface ExchangePickerProps {
  yourCards: CardInfo[]; // 4 cards: [card0, card1, exchangeCard0, exchangeCard1]
  onConfirm: (discardActions: [number, number]) => void;
}

const DISCARD_SLOT0 = 28;

export default function ExchangePicker({ yourCards, onConfirm }: ExchangePickerProps) {
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [cursor, setCursor] = useState(0);
  const [showCursor, setShowCursor] = useState(false);

  // Build card list: yours (alive original cards) + drawn
  const cards = yourCards.map((card, i) => ({
    card,
    slot: i,
    source: i < 2 ? "yours" as const : "drawn" as const,
  }));

  const yoursCards = cards.filter(c => c.source === "yours");
  const drawnCards = cards.filter(c => c.source === "drawn");
  const keepCount = yoursCards.filter(c => c.card.alive).length;
  const remaining = keepCount - selected.size;

  const toggle = useCallback((idx: number) => {
    setSelected(prev => {
      const next = new Set(prev);
      if (next.has(idx)) {
        next.delete(idx);
      } else if (next.size < keepCount) {
        next.add(idx);
      }
      return next;
    });
  }, [keepCount]);

  const confirm = useCallback(() => {
    if (selected.size !== keepCount) return;
    // Discard the UNselected cards
    const discardSlots = cards
      .map((c, i) => ({ slot: c.slot, idx: i }))
      .filter(({ idx }) => !selected.has(idx))
      .map(({ slot }) => slot)
      .sort((a, b) => a - b);
    onConfirm([
      DISCARD_SLOT0 + discardSlots[0],
      DISCARD_SLOT0 + discardSlots[1],
    ]);
  }, [selected, keepCount, cards, onConfirm]);

  useKeyboard(
    {
      ArrowLeft: () => { setShowCursor(true); setCursor(c => Math.max(0, c - 1)); },
      ArrowRight: () => { setShowCursor(true); setCursor(c => Math.min(cards.length - 1, c + 1)); },
      ArrowUp: () => { setShowCursor(true); setCursor(c => {
        // Jump from drawn row to yours row
        if (c >= yoursCards.length) {
          const col = c - yoursCards.length;
          return Math.min(col, yoursCards.length - 1);
        }
        return c;
      }); },
      ArrowDown: () => { setShowCursor(true); setCursor(c => {
        // Jump from yours row to drawn row
        if (c < yoursCards.length) {
          return yoursCards.length + Math.min(c, drawnCards.length - 1);
        }
        return c;
      }); },
      " ": () => { setShowCursor(true); toggle(cursor); },
      Enter: confirm,
    },
    [cursor, toggle, confirm, cards.length, yoursCards.length, drawnCards.length],
    true,
  );

  const hint = remaining === 0
    ? `Select ${keepCount} to KEEP: arrows move, Space toggle, Enter confirm`
    : `Select ${keepCount} to KEEP: arrows move, Space toggle (${remaining} more)`;

  return (
    <div className="border-t border-border-term pt-2">
      <div className="text-text-bright text-sm mb-1">{hint}</div>

      {/* Yours row */}
      <div className="flex gap-2 justify-center mb-1">
        {yoursCards.map((c, i) => {
          const idx = i; // index in full cards array
          return (
            <CardOption
              key={idx}
              card={c.card}
              isSelected={selected.has(idx)}
              isCursor={cursor === idx}
              onClick={() => toggle(idx)}
            />
          );
        })}
      </div>

      {/* Drawn row */}
      <div className="flex gap-2 justify-center">
        {drawnCards.map((c, i) => {
          const idx = yoursCards.length + i; // index in full cards array
          return (
            <CardOption
              key={idx}
              card={c.card}
              isSelected={selected.has(idx)}
              isCursor={cursor === idx}
              onClick={() => toggle(idx)}
            />
          );
        })}
      </div>

      {/* Mobile confirm button — hidden on desktop where Enter key works */}
      <button
        onClick={confirm}
        disabled={selected.size !== keepCount}
        className={`sm:hidden w-full mt-1 py-1.5 border text-sm transition-colors ${
          selected.size === keepCount
            ? "border-you text-you hover:bg-you/10"
            : "border-border-term text-text-dim opacity-40"
        }`}
      >
        {">> CONFIRM EXCHANGE <<"}
      </button>

      <div className="hidden sm:block text-center text-text-dim text-xs mt-1">
        Space to toggle | Enter to confirm | arrows to move
      </div>
    </div>
  );
}

function CardOption({
  card,
  isSelected,
  isCursor,
  onClick,
}: {
  card: CardInfo;
  isSelected: boolean;
  isCursor: boolean;
  onClick: () => void;
}) {
  const name = ROLE_NAMES[card.type] ?? "???";
  const color = ROLE_COLORS[name] ?? "#888";
  const checkbox = isSelected ? "[X]" : "[ ]";
  const checkColor = isSelected ? "text-cursor" : "text-text-dim";

  return (
    <button
      onClick={onClick}
      className={`text-left text-sm px-2 py-1 whitespace-nowrap transition-colors ${
        isCursor
          ? "bg-selection-bg text-text-bright"
          : "text-text-dim hover:bg-selection-bg hover:text-text-bright"
      }`}
    >
      <span className={`${checkColor} mr-1`}>{checkbox}</span>
      <span style={{ color }}>{name}</span>
    </button>
  );
}
