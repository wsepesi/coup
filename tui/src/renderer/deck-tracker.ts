// Deck tracker box — shows deck size and observed card movements

import { OptimizedBuffer, RGBA } from "@opentui/core";
import { CardType } from "@coup/game-client";
import { COLORS, CARD_ABBREV, getCardColors } from "../constants.js";

export interface DeckEvent {
  card?: CardType;       // undefined = unknown card
  direction: "in" | "out" | "swap";
  description: string;
}

export function renderDeckTracker(
  buffer: OptimizedBuffer,
  deckSize: number,
  events: DeckEvent[],
  x: number,
  y: number,
  width: number,
  height: number,
): void {
  const bg = RGBA.fromHex(COLORS.bg);
  const border = RGBA.fromHex(COLORS.border);
  const bright = RGBA.fromHex(COLORS.textBright);
  const dim = RGBA.fromHex(COLORS.textDim);
  const cardColors = getCardColors();

  buffer.drawBox({
    x, y, width, height,
    border: true,
    borderColor: border,
    backgroundColor: bg,
    shouldFill: true,
  });

  // Title with deck count
  const title = `Deck (${deckSize})`;
  buffer.drawText(title, x + Math.floor((width - title.length) / 2), y, bright, bg);

  // Show most recent events (newest at top)
  const innerH = height - 2;
  const innerW = width - 2;
  const visible = events.slice(-innerH).reverse();

  for (let i = 0; i < visible.length && i < innerH; i++) {
    const ev = visible[i];
    const arrow = ev.direction === "in" ? "↩" : ev.direction === "out" ? "↑" : "⇄";

    if (ev.card != null) {
      const abbrev = CARD_ABBREV[ev.card];
      const cc = RGBA.fromHex(cardColors[ev.card]);
      const line = `${arrow} ${abbrev} ${ev.description}`;
      const displayLine = line.slice(0, innerW);
      buffer.drawText(displayLine.slice(0, 2), x + 1, y + 1 + i, dim, bg);
      buffer.drawText(abbrev, x + 3, y + 1 + i, cc, bg);
      const rest = ` ${ev.description}`.slice(0, innerW - 2 - abbrev.length);
      if (rest) {
        buffer.drawText(rest, x + 3 + abbrev.length, y + 1 + i, dim, bg);
      }
    } else {
      const line = `${arrow} ${ev.description}`;
      buffer.drawText(line.slice(0, innerW), x + 1, y + 1 + i, dim, bg);
    }
  }

  if (events.length === 0) {
    buffer.drawText("no movements", x + 1, y + 1, dim, bg);
  }
}
