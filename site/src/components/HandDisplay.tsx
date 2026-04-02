"use client";

import type { CardInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS, ROLE_SYMBOLS, ROLE_SHORT } from "@/lib/constants";

interface HandDisplayProps {
  cards: CardInfo[];
  coins: number;
}

export default function HandDisplay({ cards, coins }: HandDisplayProps) {
  return (
    <div className="flex flex-col items-center gap-1">
      {/* Card boxes side by side, TUI style */}
      <div className="flex gap-1">
        {cards.map((card, i) => {
          const name = ROLE_NAMES[card.type] ?? "???";
          const color = ROLE_COLORS[name] ?? "#888";
          const symbol = ROLE_SYMBOLS[name] ?? "???";

          if (!card.alive) {
            return (
              <div
                key={i}
                className="border w-28 h-20 flex flex-col items-center justify-center"
                style={{ borderColor: "#999", color: "#999" }}
              >
                <span className="text-sm">[DEAD]</span>
                <span className="text-xs text-text-dim">{ROLE_SHORT[name]}</span>
              </div>
            );
          }

          return (
            <div
              key={i}
              className="border w-28 h-20 flex flex-col items-center justify-center"
              style={{ borderColor: color, color }}
            >
              <span className="text-sm font-bold">{name}</span>
              <span className="text-base">{symbol}</span>
            </div>
          );
        })}
      </div>
      {/* Coins below, TUI style */}
      <div className="text-text-default text-sm">
        {coins}● coins
      </div>
    </div>
  );
}
