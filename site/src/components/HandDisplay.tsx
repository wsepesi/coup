"use client";

import type { CardInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SYMBOLS, ROLE_SHORT, ROLE_ABILITY } from "@/lib/constants";

interface HandDisplayProps {
  cards: CardInfo[];
  coins: number;
  claims: string[];
  /** Highlight cards that can be chosen (lose-influence decision). */
  selectable?: boolean;
  onSelect?: (slot: number) => void;
}

export function CardBox({ type, alive = true, label, onClick, compact }: {
  type: number;
  alive?: boolean;
  label?: string;
  onClick?: () => void;
  compact?: boolean;
}) {
  const name = ROLE_NAMES[type] ?? "???";
  const color = ROLE_COLORS[name] ?? "#888";
  const size = compact ? "w-20 h-14 sm:w-24 sm:h-16" : "w-24 h-16 sm:w-28 sm:h-20";
  const Tag = onClick ? "button" : "div";

  if (!alive) {
    return (
      <div className="flex flex-col items-center">
        <div className={`border border-dashed ${size} flex flex-col items-center justify-center text-text-dim opacity-60`} title={`${name} — lost`}>
          <span className="text-xs line-through">{name}</span>
          <span className="text-[0.65rem]">[lost]</span>
        </div>
        {label && <span className="text-xs text-text-dim mt-0.5">{label}</span>}
      </div>
    );
  }

  return (
    <div className="flex flex-col items-center">
      <Tag
        onClick={onClick}
        className={`border-2 ${size} flex flex-col items-center justify-center bg-bg transition-transform ${onClick ? "cursor-pointer hover:-translate-y-0.5 focus-visible:-translate-y-0.5 outline-none focus-visible:ring-2 ring-cursor" : ""}`}
        style={{ borderColor: color, color }}
        title={`${name}: ${ROLE_ABILITY[name] ?? ""}`}
        aria-label={onClick ? `Reveal ${name}` : name}
      >
        <span className="text-xs sm:text-sm font-bold">{name}</span>
        <span className="text-sm sm:text-base" aria-hidden>{ROLE_SYMBOLS[name]}</span>
      </Tag>
      {label && <span className="text-xs text-text-dim mt-0.5">{label}</span>}
    </div>
  );
}

export default function HandDisplay({ cards, coins, claims, selectable, onSelect }: HandDisplayProps) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="flex gap-2">
        {cards.map((card, i) => (
          <CardBox
            key={i}
            type={card.type}
            alive={card.alive}
            onClick={selectable && card.alive && onSelect ? () => onSelect(i) : undefined}
          />
        ))}
      </div>
      <div className="flex items-center gap-3 text-sm">
        <span className="text-text-default tabular-nums">{coins}● coins</span>
        {claims.length > 0 && (
          <span className="text-xs text-text-dim">
            claimed:{" "}
            {claims.map((c) => (
              <span key={c} className="ml-1" style={{ color: ROLE_COLORS[c] }}>{ROLE_SHORT[c]}</span>
            ))}
          </span>
        )}
      </div>
    </div>
  );
}
