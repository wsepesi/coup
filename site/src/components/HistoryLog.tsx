"use client";

import { useState, useEffect, useRef, useCallback } from "react";
import type { HistoryEntry, HistoryKind } from "@/lib/types";

const KIND_CLASS: Record<HistoryKind, string> = {
  turn: "text-text-dim",
  action: "text-text-default",
  claim: "text-text-default",
  challenge: "text-dead font-bold",
  block: "text-role-captain",
  reveal: "text-text-bright font-bold",
  lose: "text-coin-loss",
  elim: "text-dead font-bold",
  info: "text-text-dim italic",
  win: "text-you font-bold",
};

function Entries({ entries, highlightLast }: { entries: HistoryEntry[]; highlightLast?: boolean }) {
  let prevTurn = -1;
  return (
    <>
      {entries.map((e, i) => {
        const newTurn = e.turn !== prevTurn && e.turn > 0;
        prevTurn = e.turn;
        const isLast = highlightLast && i === entries.length - 1;
        return (
          <div key={i} className={newTurn && i > 0 ? "mt-1 pt-1 border-t border-border-term/40" : ""}>
            <div className={`text-xs sm:text-sm leading-snug ${KIND_CLASS[e.kind] ?? ""} ${isLast ? "bg-selection-bg/30" : ""}`}>
              {newTurn && <span className="text-text-dim text-[0.65rem] mr-1.5 tabular-nums">T{e.turn}</span>}
              {e.text}
            </div>
          </div>
        );
      })}
    </>
  );
}

export function HistoryOverlay({ entries, onClose, onCopy, copied }: {
  entries: HistoryEntry[];
  onClose: () => void;
  onCopy?: () => void;
  copied?: boolean;
}) {
  const endRef = useRef<HTMLDivElement>(null);
  useEffect(() => { endRef.current?.scrollIntoView(); }, []);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        onClose();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [onClose]);
  return (
    <div className="fixed inset-0 z-50 bg-bg flex flex-col font-mono" role="dialog" aria-label="Game history">
      <div className="border-b border-border-term px-4 py-2 flex items-center justify-between">
        <span className="text-text-bright text-sm">History ({entries.length} events)</span>
        <div className="flex items-center gap-2">
          {onCopy && (
            <button onClick={onCopy} className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5">
              {copied ? "copied!" : "copy"}
            </button>
          )}
          <button onClick={onClose} className="text-text-dim hover:text-text-default text-sm border border-border-term px-2 py-0.5">
            ✕ close <span className="hidden sm:inline text-xs">(Esc)</span>
          </button>
        </div>
      </div>
      <div className="flex-1 overflow-y-auto px-4 py-2 max-w-3xl w-full mx-auto">
        {entries.length === 0 ? <div className="text-text-dim text-sm">No events yet.</div> : <Entries entries={entries} />}
        <div ref={endRef} />
      </div>
    </div>
  );
}

export default function HistoryLog({ entries }: { entries: HistoryEntry[] }) {
  const [showOverlay, setShowOverlay] = useState(false);
  const scrollRef = useRef<HTMLDivElement>(null);
  const stick = useRef(true);

  // Follow new entries unless the user scrolled up to read.
  useEffect(() => {
    const el = scrollRef.current;
    if (el && stick.current) el.scrollTop = el.scrollHeight;
  }, [entries.length]);

  const onScroll = useCallback(() => {
    const el = scrollRef.current;
    if (el) stick.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
  }, []);

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const tag = (document.activeElement as HTMLElement)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;
      if (e.key === "h" || e.key === "H") {
        e.preventDefault();
        e.stopPropagation();
        setShowOverlay((v) => !v);
      } else if (showOverlay && e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        setShowOverlay(false);
      } else if (showOverlay) {
        e.stopPropagation(); // keep game shortcuts from firing behind the overlay
      }
    };
    window.addEventListener("keydown", handler, true);
    return () => window.removeEventListener("keydown", handler, true);
  }, [showOverlay]);

  return (
    <>
      <div className="border border-border-term flex flex-col min-h-[4rem] h-full">
        <div className="flex items-center justify-between border-b border-border-term px-2 sm:px-3 py-0.5 text-text-dim text-xs shrink-0">
          <span>log</span>
          <button onClick={() => setShowOverlay(true)} className="hover:text-text-default">full history (H)</button>
        </div>
        <div ref={scrollRef} onScroll={onScroll} className="flex-1 overflow-y-auto px-2 sm:px-3 py-1" aria-live="polite" aria-relevant="additions">
          {entries.length === 0 ? <div className="text-text-dim text-sm">Game starting…</div> : <Entries entries={entries} highlightLast />}
        </div>
      </div>
      {showOverlay && <HistoryOverlay entries={entries} onClose={() => setShowOverlay(false)} />}
    </>
  );
}
