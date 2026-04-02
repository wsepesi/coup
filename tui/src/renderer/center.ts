// T15: Center action display box

import { OptimizedBuffer, RGBA } from "@opentui/core";
import { COLORS } from "../constants.js";
import { CENTER_BOX_WIDTH, CENTER_BOX_HEIGHT } from "../layout.js";

const col = (hex: string) => RGBA.fromHex(hex);

export function renderCenter(
  buffer: OptimizedBuffer,
  text: string[],
  cx: number,
  cy: number,
): void {
  const bx = cx - Math.floor(CENTER_BOX_WIDTH / 2);
  const by = cy - Math.floor(CENTER_BOX_HEIGHT / 2);

  // Draw bordered box
  buffer.drawBox({
    x: bx,
    y: by,
    width: CENTER_BOX_WIDTH,
    height: CENTER_BOX_HEIGHT,
    border: true,
    borderColor: col(COLORS.border),
    backgroundColor: col(COLORS.bg),
    shouldFill: true,
  });

  // Draw each text line centered within the box
  const innerWidth = CENTER_BOX_WIDTH - 2;
  const startY = by + 1 + Math.floor((CENTER_BOX_HEIGHT - 2 - text.length) / 2);

  for (let i = 0; i < text.length; i++) {
    const line = text[i];
    const tx = bx + 1 + Math.floor((innerWidth - line.length) / 2);
    buffer.drawText(line, tx, startY + i, col(COLORS.textDefault));
  }
}
