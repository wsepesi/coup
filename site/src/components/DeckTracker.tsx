"use client";

import type { DeckEvent } from "@/hooks/useGameTracker";
import type { CardInfo, PlayerInfo } from "@/lib/types";
import { ROLE_COLORS, ROLE_NAMES } from "@/lib/constants";

interface DeckTrackerProps {
  deckSize: number;
  events: DeckEvent[];
  players: PlayerInfo[];
  yourCards: CardInfo[];
  maxEvents?: number;
}

/** Card counting aid: how many of each role are accounted for (face-up anywhere, or in your hand). */
export default function DeckTracker({ deckSize, events, players, yourCards, maxEvents = 5 }: DeckTrackerProps) {
  const revealed = [0, 0, 0, 0, 0];
  for (const p of players) for (const t of p.revealed) revealed[t]++;
  const mine = [0, 0, 0, 0, 0];
  for (const c of yourCards) if (c.alive) mine[c.type]++;

  return (
    <div className="border border-border-term flex flex-col h-full">
      <div className="border-b border-border-term px-2 py-0.5 text-text-dim text-xs flex justify-between shrink-0">
        <span>cards</span>
        <span>deck {deckSize}</span>
      </div>
      <div className="px-2 py-1 text-xs space-y-0.5" title="face-up + in your hand, out of 3">
        {ROLE_NAMES.map((name, t) => {
          const known = revealed[t] + mine[t];
          return (
            <div key={name} className="flex items-center justify-between">
              <span style={{ color: ROLE_COLORS[name] }}>{name}</span>
              <span className="tabular-nums text-text-dim">
                {"●".repeat(revealed[t])}
                <span className="text-you">{"●".repeat(mine[t])}</span>
                {"○".repeat(Math.max(0, 3 - known))}
              </span>
            </div>
          );
        })}
        <div className="text-[0.65rem] text-text-dim pt-0.5">● lost · <span className="text-you">●</span> yours · ○ unseen</div>
      </div>
      {events.length > 0 && (
        <div className="border-t border-border-term px-2 py-1 overflow-hidden">
          {events.slice(0, maxEvents).map((event, i) => (
            <div key={i} className="text-xs leading-snug truncate">
              <span className="text-text-dim mr-1">{event.arrow}</span>
              <span style={event.card ? { color: ROLE_COLORS[event.card] ?? "#888" } : undefined} className={event.card ? "" : "text-text-dim"}>
                {event.text}
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
