// T16: Your hand display at bottom center

import { OptimizedBuffer, RGBA } from "@opentui/core";
import { CardType } from "@coup/game-client";
import type { CardState } from "@coup/game-client";
import { COLORS, getCardColors, CARD_SYMBOLS } from "../constants.js";


const col = (hex: string) => RGBA.fromHex(hex);

const CARD_NAMES: Record<CardType, string> = {
  [CardType.Duke]: "Duke",
  [CardType.Assassin]: "Assassin",
  [CardType.Captain]: "Captain",
  [CardType.Ambassador]: "Ambassador",
  [CardType.Contessa]: "Contessa",
};

const CARD_BOX_WIDTH = 11;
const CARD_BOX_HEIGHT = 5;

export function renderHand(
  buffer: OptimizedBuffer,
  cards: [CardState, CardState],
  coins: number,
  termWidth: number,
  y: number,
  selectedSlot?: number,
): void {
  const totalWidth = CARD_BOX_WIDTH * 2 + 1; // two cards + gap
  const startX = Math.floor(termWidth / 2) - Math.floor(totalWidth / 2);

  for (let i = 0; i < 2; i++) {
    const card = cards[i];
    const cx = startX + i * (CARD_BOX_WIDTH + 1);
    const isSelected = selectedSlot === i;
    const isDead = !card.alive;

    // Border color: bright white if selected, dim otherwise
    const borderColor = isSelected
      ? col(COLORS.textBright)
      : col(COLORS.border);

    buffer.drawBox({
      x: cx,
      y,
      width: CARD_BOX_WIDTH,
      height: CARD_BOX_HEIGHT,
      border: true,
      borderColor,
      backgroundColor: col(COLORS.bg),
      shouldFill: true,
    });

    if (isDead) {
      // Dead card: gray [DEAD] text
      const deadText = "[DEAD]";
      const tx = cx + Math.floor((CARD_BOX_WIDTH - deadText.length) / 2);
      buffer.drawText(deadText, tx, y + 2, col(COLORS.textDim));
    } else {
      // Card name centered
      const name = CARD_NAMES[card.type] ?? "???";
      const cardColor = col(getCardColors()[card.type] ?? COLORS.textDefault);
      const tx = cx + Math.floor((CARD_BOX_WIDTH - name.length) / 2);
      buffer.drawText(name, tx, y + 1, cardColor);

      // Symbol line centered
      const sym = CARD_SYMBOLS[card.type] ?? "???";
      const sx = cx + Math.floor((CARD_BOX_WIDTH - sym.length) / 2);
      buffer.drawText(sym, sx, y + 3, cardColor);
    }
  }

  // Coins below cards
  const coinStr = `${coins}● coins`;
  const coinX = Math.floor(termWidth / 2) - Math.floor(coinStr.length / 2);
  buffer.drawText(coinStr, coinX, y + CARD_BOX_HEIGHT, col(COLORS.textDefault));
}
