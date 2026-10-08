import { useMemo } from "react";
import type { HistoryEntry, PlayerInfo } from "@/lib/types";

export interface DeckEvent {
  arrow: string; // ↩ ⇄
  text: string;
  card?: string;
}

interface TrackerResult {
  /** seat → short description of their last main action */
  lastActions: Map<number, string>;
  /** Recent deck/reveal events (newest first) */
  deckEvents: DeckEvent[];
}

// Patterns match the narration produced by workers/src/game-room.ts (apply()).
const ACTION_PATTERNS: [RegExp, (m: RegExpMatchArray) => string][] = [
  [/^(.+?) takes Income/, () => "Income"],
  [/^(.+?) takes Foreign Aid/, () => "Foreign Aid"],
  [/^(.+?) claims Duke to take Tax/, () => "Tax"],
  [/^(.+?) claims Ambassador to Exchange/, () => "Exchange"],
  [/^(.+?) pays 7 to Coup (.+?)\.$/, (m) => `Coup → ${m[2]}`],
  [/^(.+?) claims Captain to steal from (.+?)\.$/, (m) => `Steal → ${m[2]}`],
  [/^(.+?) pays 3 and claims Assassin to assassinate (.+?)\.$/, (m) => `Assassinate → ${m[2]}`],
];

const DECK_PATTERNS: [RegExp, (m: RegExpMatchArray) => DeckEvent][] = [
  [/^(.+?) exchanged cards/, (m) => ({ arrow: "⇄", text: `${m[1]} exchanged` })],
  [/^(.+?) loses (\w+)\.$/, (m) => ({ arrow: "↓", text: `${m[2]} lost (${m[1]})`, card: m[2] })],
  [/^(.+?) reveals a (\w+) /, (m) => ({ arrow: "↩", text: `${m[2]} reshuffled`, card: m[2] })],
];

export function useGameTracker(history: HistoryEntry[], players: PlayerInfo[]): TrackerResult {
  return useMemo(() => {
    const lastActions = new Map<number, string>();
    const deckEvents: DeckEvent[] = [];
    const nameSeat = new Map<string, number>();
    players.forEach((p, i) => nameSeat.set(p.name, i));

    for (const entry of history) {
      if (entry.kind === "action" || entry.kind === "claim") {
        for (const [pattern, extract] of ACTION_PATTERNS) {
          const m = entry.text.match(pattern);
          if (m) {
            const seat = nameSeat.get(m[1]);
            if (seat != null) lastActions.set(seat, extract(m));
            break;
          }
        }
      }
      for (const [pattern, extract] of DECK_PATTERNS) {
        const m = entry.text.match(pattern);
        if (m) {
          deckEvents.push(extract(m));
          break;
        }
      }
    }
    deckEvents.reverse();
    return { lastActions, deckEvents };
  }, [history, players]);
}
