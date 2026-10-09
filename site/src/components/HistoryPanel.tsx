"use client";

import { useState, useEffect, useLayoutEffect, useMemo, useRef, Fragment } from "react";
import type { HistoryEntry, HistoryKind } from "@/lib/types";

// The table's perfect recall: every logged event, presented two ways.
//   everything — the full written transcript, grouped by turn, oldest first.
//   swimlanes  — one column per player, one row per turn; each cell is what
//                that player did during that turn (acted, passed, blocked,
//                challenged, revealed, lost a card).
// Nothing here is derived from the history beyond regrouping it.

export type HistoryTab = "everything" | "swimlanes";

const KIND_CLASS: Record<HistoryKind, string> = {
  turn: "text-text-dim",
  action: "text-text-default",
  claim: "text-text-default",
  challenge: "text-dead font-bold",
  block: "text-role-captain",
  pass: "text-text-dim",
  reveal: "text-text-bright font-bold",
  lose: "text-coin-loss",
  elim: "text-dead font-bold",
  info: "text-text-dim italic",
  win: "text-you font-bold",
};

interface Seat {
  name: string;
  /** Raw table facts for the swimlane header, when known. */
  coins?: number;
  influence?: number;
  alive?: boolean;
}

interface Turn {
  turn: number;
  entries: HistoryEntry[];
}

function groupByTurn(entries: HistoryEntry[]): Turn[] {
  const turns: Turn[] = [];
  for (const e of entries) {
    const last = turns[turns.length - 1];
    if (last && last.turn === e.turn) last.entries.push(e);
    else turns.push({ turn: e.turn, entries: [e] });
  }
  return turns; // oldest first; the panel keeps itself scrolled to the newest
}

/** "Theo passes." → "passes" when it sits in Theo's own column. */
function stripName(text: string, name: string): string {
  const t = text.startsWith(name + " ") ? text.slice(name.length + 1) : text;
  return t.replace(/[.!]$/, "");
}

function Everything({ turns }: { turns: Turn[] }) {
  return (
    <div className="px-3 py-1">
      {turns.map((t, i) => (
        <div key={t.turn} className={`py-1.5 ${i > 0 ? "border-t border-dotted border-border-term/60" : ""} ${i === turns.length - 1 ? "bg-selection-bg/20 -mx-3 px-3" : ""}`}>
          {t.entries.map((e, k) => (
            <div key={k} className="flex gap-2 text-xs sm:text-[0.8rem] leading-snug">
              <span className="w-7 shrink-0 text-right text-text-dim tabular-nums">{k === 0 ? (t.turn > 0 ? t.turn : "—") : ""}</span>
              <span className={KIND_CLASS[e.kind] ?? ""}>{e.text}</span>
            </div>
          ))}
        </div>
      ))}
    </div>
  );
}

function Swimlanes({ turns, seats, you }: { turns: Turn[]; seats: Seat[]; you: number }) {
  const n = seats.length;
  const tagged = turns.some((t) => t.entries.some((e) => e.seat != null));
  if (!tagged && turns.length > 0) {
    return <div className="p-3 text-text-dim text-sm">Swimlanes need per-player history from the server. Use “everything” for this game.</div>;
  }
  // Columns wrap their text; on narrow screens the grid keeps a minimum
  // width per player and the panel scrolls sideways.
  const cols = `2.25rem repeat(${n}, minmax(6rem, 1fr))`;
  return (
    <div className="text-xs sm:text-[0.8rem] leading-snug" style={{ minWidth: `calc(2.25rem + ${n} * 6rem)` }}>
      <div className="grid sticky top-0 z-10 bg-bg border-b border-border-term" style={{ gridTemplateColumns: cols }}>
        <span className="px-1.5 py-1.5 text-text-dim">T</span>
        {seats.map((s, i) => (
          <span key={i} className={`px-2 py-1.5 border-l border-border-term/50 truncate ${i === you ? "text-you" : ""} ${s.alive === false ? "text-text-dim line-through" : ""}`}>
            <span className="font-bold">{s.name}</span>
            {s.coins != null && s.alive !== false && (
              <span className="text-text-dim"> {s.coins}● {"▮".repeat(s.influence ?? 0)}</span>
            )}
          </span>
        ))}
      </div>
      {turns.map((t, ti) => {
        const bySeat: HistoryEntry[][] = Array.from({ length: n }, () => []);
        const notes: HistoryEntry[] = [];
        for (const e of t.entries) {
          if (e.seat != null && e.seat >= 0 && e.seat < n) bySeat[e.seat].push(e);
          else notes.push(e);
        }
        // The turn's main action is its first seat-tagged entry.
        const actor = t.entries.find((e) => e.seat != null && (e.kind === "action" || e.kind === "claim"))?.seat;
        return (
          <Fragment key={t.turn}>
            <div className={`grid border-b border-dotted border-border-term/60 ${ti === turns.length - 1 ? "bg-selection-bg/20" : ""}`} style={{ gridTemplateColumns: cols }}>
              <span className="px-1.5 py-1.5 text-text-dim tabular-nums">{t.turn > 0 ? t.turn : "—"}</span>
              {bySeat.map((list, s) => (
                <div key={s} className={`px-2 py-1.5 border-l border-border-term/50 ${s === actor ? "bg-selection-bg/40" : ""}`}>
                  {list.map((e, k) => (
                    <div key={k} className={`${KIND_CLASS[e.kind] ?? ""} ${s === actor && k === 0 ? "font-bold" : ""}`}>
                      {stripName(e.text, seats[s].name)}
                    </div>
                  ))}
                </div>
              ))}
            </div>
            {notes.length > 0 && (
              <div className="grid border-b border-dotted border-border-term/60" style={{ gridTemplateColumns: cols }}>
                <span />
                <div className="px-2 py-1 text-text-dim italic" style={{ gridColumn: `span ${n}` }}>
                  {notes.map((e) => e.text).join(" · ")}
                </div>
              </div>
            )}
          </Fragment>
        );
      })}
    </div>
  );
}

export default function HistoryPanel({ entries, seats, you = -1, tab: tabProp, onTab, toolbar }: {
  entries: HistoryEntry[];
  seats: Seat[];
  you?: number;
  tab?: HistoryTab;
  onTab?: (t: HistoryTab) => void;
  toolbar?: React.ReactNode;
}) {
  const [own, setOwn] = useState<HistoryTab>("everything");
  const tab = tabProp ?? own;
  const setTab = onTab ?? setOwn;
  const turns = useMemo(() => groupByTurn(entries), [entries]);

  // Follow new events at the bottom, unless the reader has scrolled up.
  const scroller = useRef<HTMLDivElement>(null);
  const following = useRef(true);
  const onScroll = () => {
    const el = scroller.current;
    if (el) following.current = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
  };
  useLayoutEffect(() => {
    const el = scroller.current;
    if (el && following.current) el.scrollTop = el.scrollHeight;
  }, [entries, tab]);

  // H flips between the two views.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const tag = (document.activeElement as HTMLElement)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;
      if (e.key === "h" || e.key === "H") {
        e.preventDefault();
        setTab(tab === "everything" ? "swimlanes" : "everything");
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [tab, setTab]);

  return (
    <div className="border border-border-term flex flex-col min-h-0 h-full">
      <div className="flex items-stretch border-b border-border-term text-xs shrink-0" role="tablist" aria-label="History view">
        {(["everything", "swimlanes"] as const).map((t) => (
          <button
            key={t}
            role="tab"
            aria-selected={tab === t}
            onClick={() => setTab(t)}
            className={`px-3 py-1.5 border-r border-border-term ${tab === t ? "bg-cursor text-bg" : "text-text-dim hover:text-text-default"}`}
          >
            {t}
          </button>
        ))}
        <span className="ml-auto flex items-center gap-2 px-2 text-text-dim">
          {toolbar}
          <span className="hidden sm:inline">oldest first · H to switch</span>
        </span>
      </div>
      <div ref={scroller} onScroll={onScroll} className="flex-1 min-h-0 overflow-auto" role="tabpanel" aria-live="polite" aria-relevant="additions">
        {entries.length === 0 ? (
          <div className="p-3 text-text-dim text-sm">Game starting…</div>
        ) : tab === "everything" ? (
          <Everything turns={turns} />
        ) : (
          <Swimlanes turns={turns} seats={seats} you={you} />
        )}
      </div>
    </div>
  );
}

/** Full-screen history (end-of-game screen). */
export function HistoryOverlay({ entries, seats, onClose, onCopy, copied }: {
  entries: HistoryEntry[];
  seats: Seat[];
  onClose: () => void;
  onCopy?: () => void;
  copied?: boolean;
}) {
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
    <div className="fixed inset-0 z-50 bg-bg flex flex-col font-mono p-2 sm:p-4" role="dialog" aria-label="Game history">
      <HistoryPanel
        entries={entries}
        seats={seats}
        toolbar={
          <>
            {onCopy && (
              <button onClick={onCopy} className="hover:text-text-default border border-border-term px-2 py-0.5">
                {copied ? "copied!" : "copy"}
              </button>
            )}
            <button onClick={onClose} className="hover:text-text-default border border-border-term px-2 py-0.5">
              ✕ close <span className="hidden sm:inline">(Esc)</span>
            </button>
          </>
        }
      />
    </div>
  );
}
