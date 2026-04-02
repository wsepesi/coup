"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import type { HistoryEntry } from "@/lib/types";

interface HistoryLogProps {
  entries: HistoryEntry[];
}

const OVERLAY_VISIBLE_LINES = 20;

export default function HistoryLog({ entries }: HistoryLogProps) {
  const [showOverlay, setShowOverlay] = useState(false);
  const [scrollOffset, setScrollOffset] = useState(0);
  const bottomRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to bottom when new entries arrive
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [entries.length]);

  // Reset scroll when overlay opens
  useEffect(() => {
    if (showOverlay) {
      setScrollOffset(Math.max(0, entries.length - OVERLAY_VISIBLE_LINES));
    }
  }, [showOverlay, entries.length]);

  // Keyboard handler with capture to intercept before ActionPicker
  const handleKeyDown = useCallback(
    (e: KeyboardEvent) => {
      const tag = (document.activeElement as HTMLElement)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;

      if (e.key === "h" || e.key === "H") {
        e.preventDefault();
        e.stopPropagation();
        setShowOverlay((v) => !v);
        return;
      }

      if (!showOverlay) return;

      if (e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        setShowOverlay(false);
        return;
      }

      if (e.key === "ArrowUp") {
        e.preventDefault();
        e.stopPropagation();
        setScrollOffset((s) => Math.max(0, s - 1));
      } else if (e.key === "ArrowDown") {
        e.preventDefault();
        e.stopPropagation();
        setScrollOffset((s) => Math.min(Math.max(0, entries.length - OVERLAY_VISIBLE_LINES), s + 1));
      }
    },
    [showOverlay, entries.length],
  );

  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown, true);
    return () => window.removeEventListener("keydown", handleKeyDown, true);
  }, [handleKeyDown]);

  return (
    <>
      {/* Scrollable history — flex-grows to fill available space */}
      <div className="border border-border-term flex flex-col min-h-[4rem] h-full">
        <div className="flex items-center justify-between border-b border-border-term px-2 sm:px-3 py-0.5 text-text-dim text-xs shrink-0">
          <span>{"═".repeat(16)}</span>
          <button onClick={() => setShowOverlay(true)} className="sm:pointer-events-none">(H) full history</button>
        </div>
        <div className="flex-1 overflow-y-auto px-2 sm:px-3 py-1">
          {entries.length === 0 ? (
            <div className="text-text-dim text-sm">Game starting...</div>
          ) : (
            entries.map((entry, i) => {
              const isLast = i === entries.length - 1;
              return (
                <div
                  key={i}
                  className={`text-xs sm:text-sm leading-snug ${isLast ? "text-text-bright" : "text-text-dim"}`}
                >
                  {entry.text}
                </div>
              );
            })
          )}
          <div ref={bottomRef} />
        </div>
      </div>

      {/* Fullscreen overlay */}
      {showOverlay && (
        <div className="fixed inset-0 z-50 bg-bg flex flex-col font-mono">
          <div className="border-b border-border-term px-4 py-2 flex items-center justify-between">
            <span className="text-text-bright text-sm">
              History ({entries.length} events)
            </span>
            <div className="flex items-center gap-3">
              <span className="text-text-dim text-xs hidden sm:inline">
                ↑↓ scroll | H or ESC close
              </span>
              <button
                onClick={() => setShowOverlay(false)}
                className="sm:hidden text-text-dim text-sm border border-border-term px-2 py-0.5"
              >
                ✕ close
              </button>
            </div>
          </div>
          <div className="flex-1 overflow-hidden px-4 py-2">
            {entries.length === 0 ? (
              <div className="text-text-dim text-sm">No events yet.</div>
            ) : (
              entries
                .slice(scrollOffset, scrollOffset + OVERLAY_VISIBLE_LINES)
                .map((entry, i) => {
                  const idx = scrollOffset + i;
                  return (
                    <div key={idx} className="text-sm py-0.5">
                      <span className="text-text-dim mr-2 w-6 inline-block text-right">
                        {idx + 1}.
                      </span>
                      <span className={idx === entries.length - 1 ? "text-text-bright" : "text-text-default"}>
                        {entry.text}
                      </span>
                    </div>
                  );
                })
            )}
          </div>
          <div className="border-t border-border-term px-4 py-1 text-text-dim text-xs">
            showing {scrollOffset + 1}-{Math.min(scrollOffset + OVERLAY_VISIBLE_LINES, entries.length)} of {entries.length}
          </div>
        </div>
      )}
    </>
  );
}
