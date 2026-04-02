"use client";

import type { CardInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SYMBOLS, ROLE_SHORT } from "@/lib/constants";

interface HandDisplayProps {
  cards: CardInfo[];
  coins: number;
  isExchange?: boolean;
  ownClaims?: { short: string; color: string }[];
}

function CardBox({ card, label }: { card: CardInfo; label?: string }) {
  const name = ROLE_NAMES[card.type] ?? "???";
  const color = ROLE_COLORS[name] ?? "#888";
  const symbol = ROLE_SYMBOLS[name] ?? "???";

  if (!card.alive) {
    return (
      <div className="flex flex-col items-center">
        <div
          className="border w-20 h-14 sm:w-28 sm:h-20 flex flex-col items-center justify-center"
          style={{ borderColor: "#999", color: "#999" }}
        >
          <span className="text-xs sm:text-sm">[DEAD]</span>
          <span className="text-xs text-text-dim">{ROLE_SHORT[name]}</span>
        </div>
        {label && <span className="text-xs text-text-dim mt-0.5">{label}</span>}
      </div>
    );
  }

  return (
    <div className="flex flex-col items-center">
      <div
        className="border w-20 h-14 sm:w-28 sm:h-20 flex flex-col items-center justify-center"
        style={{ borderColor: color, color }}
      >
        <span className="text-xs sm:text-sm font-bold">{name}</span>
        <span className="text-sm sm:text-base">{symbol}</span>
      </div>
      {label && <span className="text-xs text-text-dim mt-0.5">{label}</span>}
    </div>
  );
}

export default function HandDisplay({ cards, coins, isExchange, ownClaims }: HandDisplayProps) {
  if (isExchange && cards.length > 2) {
    // Exchange mode: 2 rows — yours (first 2 alive) + drawn (last 2)
    const yours = cards.slice(0, 2);
    const drawn = cards.slice(2);

    return (
      <div className="flex flex-col items-center gap-2">
        {/* Row 1: Your cards */}
        <div className="text-center">
          <div className="text-text-dim text-xs mb-1">Your cards</div>
          <div className="flex gap-1 justify-center">
            {yours.map((card, i) => (
              <CardBox key={`y-${i}`} card={card} label="(yours)" />
            ))}
          </div>
        </div>
        {/* Row 2: Drawn cards */}
        <div className="text-center">
          <div className="text-text-dim text-xs mb-1">Drawn from deck</div>
          <div className="flex gap-1 justify-center">
            {drawn.map((card, i) => (
              <CardBox key={`d-${i}`} card={card} label="(drawn)" />
            ))}
          </div>
        </div>
        <div className="text-text-default text-sm">
          {coins}● coins
        </div>
      </div>
    );
  }

  return (
    <div className="flex flex-col items-center gap-1">
      <div className="flex gap-1">
        {cards.map((card, i) => (
          <CardBox key={i} card={card} />
        ))}
      </div>
      <div className="text-text-default text-sm">
        {coins}● coins
      </div>
      {/* Own claims below coins */}
      {ownClaims && ownClaims.length > 0 && (
        <div className="flex items-center gap-1">
          {ownClaims.map((c, i) => (
            <span key={i} className="text-xs" style={{ color: c.color }}>{c.short}</span>
          ))}
        </div>
      )}
    </div>
  );
}
