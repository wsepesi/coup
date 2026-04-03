import { useMemo } from "react";
import type { HistoryEntry, PlayerInfo } from "@/lib/types";
import { ROLE_SHORT, ROLE_COLORS } from "@/lib/constants";

export interface DeckEvent {
  arrow: string; // ↑ ↩ ⇄
  text: string;
  card?: string; // role name if known
}

interface TrackerResult {
  /** seat → short description of last main action */
  lastActions: Map<number, string>;
  /** Recent deck movement events (newest first) */
  deckEvents: DeckEvent[];
}

// Main action patterns for last-action tracking
const ACTION_PATTERNS: [RegExp, (m: RegExpMatchArray) => string][] = [
  [/^(.+?) takes Income\./, () => "Income"],
  [/^(.+?) takes Foreign Aid\./, () => "Foreign Aid"],
  [/^(.+?) claims Duke for Tax\./, () => "Tax"],
  [/^(.+?) claims Ambassador for Exchange\./, () => "Exchange"],
  [/^(.+?) Coups (.+?)\./, (m) => `Coup → ${m[2]}`],
  [/^(.+?) claims Captain to Steal from (.+?)\./, (m) => `Steal → ${m[2]}`],
  [/^(.+?) claims Assassin to Assassinate (.+?)\./, (m) => `Assassinate → ${m[2]}`],
];

// Deck event patterns
const DECK_PATTERNS: [RegExp, (m: RegExpMatchArray) => DeckEvent | null][] = [
  [/^(.+?) claims Ambassador for Exchange\./, (m) => ({ arrow: "⇄", text: `${m[1]} exchanges` })],
  [/^(.+?) loses (\w+)\./, (m) => ({ arrow: "↩", text: `${m[2]} revealed`, card: m[2] })],
  [/^Challenge failed! .+ reveals (\w+)\./, (m) => ({ arrow: "↩", text: `${m[1]} reshuffled`, card: m[1] })],
];

export function useGameTracker(
  history: HistoryEntry[],
  players: PlayerInfo[],
): TrackerResult {
  return useMemo(() => {
    const lastActions = new Map<number, string>();
    const deckEvents: DeckEvent[] = [];

    // Build name→seat lookup
    const nameSeat = new Map<string, number>();
    players.forEach((p, i) => nameSeat.set(p.name, i));

    for (const entry of history) {
      const text = entry.text;

      // Last actions
      for (const [pattern, extract] of ACTION_PATTERNS) {
        const m = text.match(pattern);
        if (m) {
          const name = m[1];
          const seat = nameSeat.get(name);
          if (seat != null) {
            lastActions.set(seat, extract(m));
          }
          break;
        }
      }

      // Deck events
      for (const [pattern, extract] of DECK_PATTERNS) {
        const m = text.match(pattern);
        if (m) {
          const event = extract(m);
          if (event) deckEvents.push(event);
          break;
        }
      }
    }

    // Reverse deck events so newest is first
    deckEvents.reverse();

    return { lastActions, deckEvents };
  }, [history, players]);
}

/** Get color-coded claim abbreviations for a player */
export function getClaimDisplay(claims: Set<string> | undefined): { short: string; color: string }[] {
  if (!claims || claims.size === 0) return [];
  return Array.from(claims).map((role) => ({
    short: ROLE_SHORT[role] ?? "??",
    color: ROLE_COLORS[role] ?? "#888",
  }));
}
