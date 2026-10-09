"use client";

import type { CardInfo, HistoryEntry, PlayerInfo } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS } from "@/lib/constants";

// What you'd see sitting at the table: each player's coins, face-down cards,
// face-up (lost) cards, what they said this turn, plus your own hand, the
// deck, and who the current action points at. Two recall aids the server
// keeps: the roles each player currently claims, and the cards you saw go
// into the deck since anyone last drew from it.

interface TableProps {
  players: PlayerInfo[];
  you: number; // -1 when spectating
  cards: CardInfo[];
  turnPlayer: number;
  active: number;
  /** Seat targeted by the pending action, if any. */
  target: number | null;
  deck: number;
  shuffledIn?: { cards: number[]; seat: number; turn: number; via: "reveal" | "exchange" };
  turn: number;
  /** History of the current turn (for "passes", "blocks, claiming Duke" under each seat). */
  turnEntries: HistoryEntry[];
}

function seatPosition(rel: number, n: number) {
  // rel 0 (you) at the bottom, the rest clockwise in turn order. The x radius
  // leaves room for half a seat box at the left and right edges.
  const angle = Math.PI / 2 + (rel * 2 * Math.PI) / n;
  return { x: 50 + 30 * Math.cos(angle), y: 48 + 37 * Math.sin(angle) };
}

function lastWords(entries: HistoryEntry[], seat: number, name: string): string | null {
  for (let i = entries.length - 1; i >= 0; i--) {
    const e = entries[i];
    if (e.seat !== seat) continue;
    const t = e.text.startsWith(name + " ") ? e.text.slice(name.length + 1) : e.text;
    return t.replace(/[.!]$/, "");
  }
  return null;
}

export default function Table({ players, you, cards, turnPlayer, active, target, deck, shuffledIn, turn, turnEntries }: TableProps) {
  const n = players.length;
  const base = you >= 0 ? you : 0;
  const pos = players.map((_, s) => seatPosition((s - base + n) % n, n));
  const from = pos[turnPlayer];
  const to = target != null ? pos[target] : null;

  return (
    <div className="relative w-full h-full min-h-[18rem] border border-border-term overflow-hidden">
      <div aria-hidden className="absolute rounded-[50%] border border-dashed border-selection-bg" style={{ left: "20%", right: "20%", top: "14%", bottom: "16%" }} />

      {from && to && (
        <svg aria-hidden className="absolute inset-0 w-full h-full pointer-events-none" viewBox="0 0 100 100" preserveAspectRatio="none">
          <line x1={from.x} y1={from.y} x2={to.x} y2={to.y} stroke="var(--cursor)" strokeWidth={2} strokeDasharray="6 5" vectorEffect="non-scaling-stroke" />
        </svg>
      )}

      <div className="absolute left-1/2 top-[46%] -translate-x-1/2 -translate-y-1/2 text-center text-xs text-text-dim leading-relaxed">
        <div>turn {turn}</div>
        <div><span aria-hidden>▮▮▮</span> deck {deck}</div>
        {shuffledIn && shuffledIn.cards.length > 0 && (
          <div className="mt-1 pt-1 border-t border-dotted border-border-term max-w-[12rem]">
            <div className="text-[0.7rem]">shuffled in</div>
            <div>
              {shuffledIn.cards.map((t, i) => (
                <span key={i}>
                  {i > 0 && ", "}
                  <span style={{ color: ROLE_COLORS[ROLE_NAMES[t]] }}>{ROLE_NAMES[t]}</span>
                </span>
              ))}
            </div>
            <div className="text-[0.7rem]">
              T{shuffledIn.turn} · {shuffledIn.seat === you ? "your" : `${players[shuffledIn.seat]?.name}'s`} {shuffledIn.via}
            </div>
          </div>
        )}
      </div>

      {players.map((p, s) => {
        const isYou = s === you;
        const dead = !p.alive;
        const said = lastWords(turnEntries, s, p.name);
        const status = p.bot ? "bot" : p.away ? "away" : !p.online ? "offline" : null;
        const border = dead
          ? "border-dashed border-border-term"
          : s === active
            ? "border-2 border-cursor"
            : s === target
              ? "border-2 border-dashed border-cursor"
              : isYou ? "border-you" : "border-border-term";
        return (
          <div
            key={s}
            className={`absolute -translate-x-1/2 -translate-y-1/2 w-[7.25rem] sm:w-[11rem] bg-bg border px-1.5 sm:px-2 py-1 sm:py-1.5 text-[0.7rem] sm:text-sm leading-snug ${border} ${dead ? "text-text-dim" : ""}`}
            style={{ left: `${pos[s].x}%`, top: `${pos[s].y}%` }}
            aria-label={`${p.name}${isYou ? " (you)" : ""}: ${p.coins} coins, ${p.influence} cards${dead ? ", out" : ""}`}
          >
            <div className="flex items-baseline gap-1 min-w-0">
              {s === turnPlayer && !dead && <span aria-hidden>▶</span>}
              <span className={`truncate font-bold ${isYou ? "text-you" : ""} ${dead ? "line-through font-normal" : ""}`}>{p.name}{isYou ? " (you)" : ""}</span>
              {status && <span className="text-text-dim text-[0.7rem]">{status}</span>}
              {!dead && <span className="ml-auto tabular-nums"><span className="font-bold">{p.coins}</span>●</span>}
            </div>
            <div className="flex flex-wrap gap-x-1.5 mt-0.5">
              {isYou
                ? cards.map((c, i) => (
                    <span key={i} className={c.alive ? "font-bold" : "line-through opacity-70"} style={{ color: ROLE_COLORS[ROLE_NAMES[c.type]] }}>
                      {ROLE_NAMES[c.type]}
                    </span>
                  ))
                : <>
                    {Array.from({ length: p.influence }).map((_, i) => <span key={`h${i}`} aria-hidden>▮</span>)}
                    {p.revealed.map((t, i) => (
                      <span key={`r${i}`} className="line-through opacity-80" style={{ color: ROLE_COLORS[ROLE_NAMES[t]] }}>{ROLE_NAMES[t]}</span>
                    ))}
                  </>}
            </div>
            {!dead && (
              <div className="flex flex-wrap gap-x-1.5 mt-0.5 pt-0.5 border-t border-dotted border-border-term text-[0.7rem] sm:text-xs">
                <span className="text-text-dim">claims</span>
                {p.claims.length === 0
                  ? <span className="text-text-dim">—</span>
                  : p.claims.map((r) => <span key={r} style={{ color: ROLE_COLORS[r] }}>{r}</span>)}
              </div>
            )}
            {!dead && (said || s === active) && (
              <div className="text-text-dim text-[0.7rem] sm:text-xs truncate" title={said ?? undefined}>
                {s === active ? "deciding…" : said}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}
