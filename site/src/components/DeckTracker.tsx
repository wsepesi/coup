"use client";

import type { DeckEvent } from "@/hooks/useGameTracker";
import { ROLE_COLORS } from "@/lib/constants";

interface DeckTrackerProps {
  deckSize: number;
  events: DeckEvent[];
  maxEvents?: number;
}

export default function DeckTracker({ deckSize, events, maxEvents = 6 }: DeckTrackerProps) {
  const visible = events.slice(0, maxEvents);

  return (
    <div className="border border-border-term flex flex-col shrink-0">
      <div className="border-b border-border-term px-2 py-0.5 text-text-dim text-xs text-center shrink-0">
        Deck ({deckSize})
      </div>
      <div className="px-2 py-1 space-y-0">
        {visible.length === 0 ? (
          <div className="text-text-dim text-xs">No events</div>
        ) : (
          visible.map((event, i) => (
            <div key={i} className="text-xs leading-snug truncate">
              <span className="text-text-dim mr-1">{event.arrow}</span>
              {event.card ? (
                <span style={{ color: ROLE_COLORS[event.card] ?? "#888" }}>
                  {event.text}
                </span>
              ) : (
                <span className="text-text-dim">{event.text}</span>
              )}
            </div>
          ))
        )}
      </div>
    </div>
  );
}
