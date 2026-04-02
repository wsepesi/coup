// T14: Table renderer — draws all non-human players in oval positions

import { OptimizedBuffer, RGBA } from "@opentui/core";
import type { GameSnapshot } from "@coup/game-client";
import { describeAction, actionTarget } from "@coup/game-client";
import type { PlayerPosition } from "../layout.js";
import { COLORS, CARD_ABBREV } from "../constants.js";
import { PLAYER_DISPLAY_WIDTH } from "../layout.js";

const col = (hex: string) => RGBA.fromHex(hex);

export function renderTable(
  buffer: OptimizedBuffer,
  snapshot: GameSnapshot,
  positions: PlayerPosition[],
  humanSeat: number,
  targetSeat?: number,
  playerNames?: (seat: number) => string,
): void {
  for (const pos of positions) {
    if (pos.seat === humanSeat) continue;
    const player = snapshot.players[pos.seat];
    if (!player) continue;

    const isActive = snapshot.activePlayer === pos.seat;
    const isTurnPlayer = snapshot.turnPlayer === pos.seat;
    const isTarget = targetSeat !== undefined && targetSeat === pos.seat;
    const isDead = !player.alive;

    const px = pos.x - Math.floor(PLAYER_DISPLAY_WIDTH / 2);
    const py = pos.y;

    // --- Line 1: name ---
    const name = playerNames ? playerNames(pos.seat) : `bot-${pos.seat}`;
    const label = `P${pos.seat} ${name}`;
    let nameColor: RGBA;
    if (isTarget) {
      nameColor = col(COLORS.cursor);
    } else if (isDead) {
      nameColor = col(COLORS.textDim);
    } else if (isActive) {
      nameColor = col(COLORS.textBright);
    } else {
      nameColor = col(COLORS.bot);
    }

    if (isTarget) {
      buffer.drawText("▶", px - 2, py, col(COLORS.cursor));
    }

    buffer.drawText(label, px, py, nameColor);

    // Alive indicator
    let indicator: string;
    let indicatorColor: RGBA;
    if (isDead) {
      indicator = "☠";
      indicatorColor = col(COLORS.dead);
    } else if (player.influence === 1) {
      indicator = "○";
      indicatorColor = col(COLORS.textDim);
    } else {
      indicator = "●";
      indicatorColor = col(COLORS.textDefault);
    }
    buffer.drawText(indicator, px + label.length + 1, py, indicatorColor);

    // --- Line 2: cards + coins ---
    let cardStr = "";
    for (let i = 0; i < 2; i++) {
      const card = player.cards[i];
      if (i > 0) cardStr += " ";
      if (card.alive) {
        cardStr += "▓▓";
      } else {
        const abbrev = CARD_ABBREV[card.type] ?? "??";
        cardStr += `[${abbrev}]`;
      }
    }

    const cardLine = py + 1;
    buffer.drawText(cardStr, px, cardLine, col(COLORS.textDim));

    // Coins after cards
    const coinStr = ` ${player.coins}●`;
    buffer.drawText(coinStr, px + cardStr.length, cardLine, col(COLORS.textDefault));

    // --- Line 3: turn action indicator ---
    if (isTurnPlayer && !isDead && snapshot.pendingAction >= 0) {
      const target = actionTarget(snapshot.pendingAction);
      const targetName = target != null && playerNames ? playerNames(target) : undefined;
      const actionDesc = describeShortAction(snapshot.pendingAction, targetName);
      if (actionDesc) {
        buffer.drawText(`→ ${actionDesc}`, px, py + 2, col(COLORS.cursor));
      }
    }
  }
}

function describeShortAction(action: number, targetName?: string): string {
  const t = targetName ?? "?";
  if (action === 0) return "Income";
  if (action === 1) return "Foreign Aid";
  if (action === 2) return "Tax (Duke)";
  if (action === 3) return "Exchange (Amb)";
  if (action >= 4 && action <= 9) return `Coup → ${t}`;
  if (action >= 10 && action <= 15) return `Steal → ${t}`;
  if (action >= 16 && action <= 21) return `Assassinate → ${t}`;
  return "";
}
