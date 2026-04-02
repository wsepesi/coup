// T21: Game over screen

import { RGBA } from "@opentui/core";
import type { OptimizedBuffer } from "@opentui/core";
import { COLORS } from "../constants.js";

export interface GameResult {
  winner: number;
  winnerName: string;
  placements: { seat: number; name: string; eliminatedTurn?: number }[];
  seed: string;
  totalTurns: number;
}

export function renderGameOver(
  buffer: OptimizedBuffer,
  result: GameResult,
  width: number,
  height: number,
) {
  const bg = RGBA.fromHex(COLORS.bg);
  const bright = RGBA.fromHex(COLORS.textBright);
  const text = RGBA.fromHex(COLORS.textDefault);
  const dim = RGBA.fromHex(COLORS.textDim);
  const you = RGBA.fromHex(COLORS.you);
  const border = RGBA.fromHex(COLORS.border);

  buffer.clear(bg);

  const cx = Math.floor(width / 2);
  let y = Math.floor(height * 0.15);

  // Title
  const title = "G A M E   O V E R";
  buffer.drawText(title, cx - Math.floor(title.length / 2), y, bright, bg);
  y += 2;

  // Winner
  const winText = `Winner: ${result.winnerName}`;
  buffer.drawText(winText, cx - Math.floor(winText.length / 2), y, you, bg);
  y += 2;

  // Results header
  buffer.drawText("Results:", cx - 20, y, text, bg);
  y += 1;

  // Placements
  const ordinals = ["1st", "2nd", "3rd", "4th", "5th", "6th"];
  for (let i = 0; i < result.placements.length; i++) {
    const p = result.placements[i];
    const ord = ordinals[i] ?? `${i + 1}th`;
    const status = p.eliminatedTurn != null
      ? `eliminated turn ${p.eliminatedTurn}`
      : "survived";
    const line = `  ${ord.padEnd(5)} ${p.name.padEnd(15)} ${status}`;
    const fg = p.name === "You" || p.name === "YOU" ? you : dim;
    buffer.drawText(line, cx - 20, y, fg, bg);
    y++;
  }

  y += 1;
  // Seed and duration
  buffer.drawText(`Seed: ${result.seed}`, cx - 20, y, dim, bg);
  buffer.drawText(`Duration: ${result.totalTurns} turns`, cx, y, dim, bg);
  y += 2;

  // Options
  const optLine = "[R] Replay same seed    [N] New game    [Q] Quit";
  buffer.drawText(optLine, cx - Math.floor(optLine.length / 2), y, text, bg);
}
