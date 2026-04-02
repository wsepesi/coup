"use client";

import { useEffect, useRef } from "react";
import type { HistoryEntry } from "@/lib/types";

interface HistoryLogProps {
  entries: HistoryEntry[];
}

export default function HistoryLog({ entries }: HistoryLogProps) {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [entries.length]);

  return (
    <div className="border border-border-term flex flex-col h-36">
      <div className="border-b border-border-term px-3 py-1 text-text-dim text-xs shrink-0">
        {"═".repeat(30)}
      </div>
      <div className="flex-1 overflow-y-auto px-3 py-1 space-y-0">
        {entries.length === 0 ? (
          <div className="text-text-dim text-sm">Game starting...</div>
        ) : (
          entries.map((entry, i) => {
            const isLast = i === entries.length - 1;
            return (
              <div
                key={i}
                className={`text-sm ${isLast ? "text-text-bright" : "text-text-dim"}`}
              >
                {entry.text}
              </div>
            );
          })
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  );
}
