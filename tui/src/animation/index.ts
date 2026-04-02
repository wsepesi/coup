// T17: Animation utilities

import { OptimizedBuffer, RGBA } from "@opentui/core";
import { CardType } from "@coup/game-client";
import { COLORS, CARD_ABBREV, SPINNER_FRAMES } from "../constants.js";

const col = (hex: string) => RGBA.fromHex(hex);

export function sleep(ms: number, fast?: boolean): Promise<void> {
  if (fast) return Promise.resolve();
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Challenge reveal animation: ▓▓ -> [??] yellow -> [Dk] green/red -> settle
 */
export async function animateChallengeReveal(
  buffer: OptimizedBuffer,
  x: number,
  y: number,
  cardType: CardType,
  success: boolean,
  fast?: boolean,
): Promise<void> {
  const abbrev = CARD_ABBREV[cardType] ?? "??";

  // Frame 1: hidden
  buffer.drawText("▓▓", x, y, col(COLORS.textDim));
  await sleep(150, fast);

  // Frame 2: mystery
  buffer.drawText("[??]", x, y, col(COLORS.cursor));
  await sleep(200, fast);

  // Frame 3: reveal with result color
  const resultColor = success ? col(COLORS.challengeWin) : col(COLORS.challengeFail);
  buffer.drawText(`[${abbrev}]`, x, y, resultColor);
  await sleep(300, fast);

  // Frame 4: settle to default
  buffer.drawText(`[${abbrev}]`, x, y, col(COLORS.textDefault));
  await sleep(100, fast);
}

/**
 * Card death animation: ▓▓ -> [Dk] red -> dim
 */
export async function animateCardDeath(
  buffer: OptimizedBuffer,
  x: number,
  y: number,
  cardType: CardType,
  fast?: boolean,
): Promise<void> {
  const abbrev = CARD_ABBREV[cardType] ?? "??";

  // Frame 1: hidden
  buffer.drawText("▓▓", x, y, col(COLORS.textDim));
  await sleep(150, fast);

  // Frame 2: reveal in red
  buffer.drawText(`[${abbrev}]`, x, y, col(COLORS.dead));
  await sleep(300, fast);

  // Frame 3: fade to dim
  buffer.drawText(`[${abbrev}]`, x, y, col(COLORS.textDim));
  await sleep(200, fast);

  // Frame 4: final dim state
  buffer.drawText(`[${abbrev}]`, x, y, col(COLORS.textDim));
}

/**
 * Coin change animation: flash green/red then settle
 */
export async function animateCoinChange(
  buffer: OptimizedBuffer,
  x: number,
  y: number,
  oldCoins: number,
  newCoins: number,
  fast?: boolean,
): Promise<void> {
  const diff = newCoins - oldCoins;
  const flashColor = diff >= 0 ? col(COLORS.coinGain) : col(COLORS.coinLoss);
  const sign = diff >= 0 ? "+" : "";
  const diffStr = `${sign}${diff}`;

  // Frame 1: show old value
  buffer.drawText(`${oldCoins}●`, x, y, col(COLORS.textDefault));
  await sleep(100, fast);

  // Frame 2: flash diff
  buffer.drawText(`${diffStr} `, x, y, flashColor);
  await sleep(300, fast);

  // Frame 3: flash new value in color
  buffer.drawText(`${newCoins}●`, x, y, flashColor);
  await sleep(200, fast);

  // Frame 4: settle
  buffer.drawText(`${newCoins}●`, x, y, col(COLORS.textDefault));
}

/**
 * Braille spinner cycling every 200ms
 */
export function createSpinner(
  buffer: OptimizedBuffer,
  x: number,
  y: number,
  fast?: boolean,
): { stop: () => void } {
  if (fast) {
    // Draw single frame and return no-op stop
    buffer.drawText(SPINNER_FRAMES[0], x, y, col(COLORS.textDim));
    return { stop: () => {} };
  }

  let frameIdx = 0;
  let running = true;

  const tick = () => {
    if (!running) return;
    buffer.drawText(SPINNER_FRAMES[frameIdx], x, y, col(COLORS.textDim));
    frameIdx = (frameIdx + 1) % SPINNER_FRAMES.length;
    if (running) setTimeout(tick, 200);
  };

  tick();

  return {
    stop: () => {
      running = false;
      // Clear spinner character
      buffer.drawText(" ", x, y, col(COLORS.bg));
    },
  };
}
