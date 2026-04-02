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

  // Word-wrap and draw each text line centered within the box
  const innerWidth = CENTER_BOX_WIDTH - 2;
  const wrapped: string[] = [];
  for (const line of text) {
    if (line.length <= innerWidth) {
      wrapped.push(line);
    } else {
      const words = line.split(" ");
      let current = "";
      for (const word of words) {
        if (current.length + (current ? 1 : 0) + word.length > innerWidth) {
          if (current) wrapped.push(current);
          current = word.slice(0, innerWidth);
        } else {
          current = current ? current + " " + word : word;
        }
      }
      if (current) wrapped.push(current);
    }
  }

  const maxLines = CENTER_BOX_HEIGHT - 2;
  const lines = wrapped.slice(0, maxLines);
  const startY = by + 1 + Math.floor((CENTER_BOX_HEIGHT - 2 - lines.length) / 2);

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const tx = bx + 1 + Math.floor((innerWidth - line.length) / 2);
    buffer.drawText(line, tx, startY + i, col(COLORS.textDefault));
  }
}
