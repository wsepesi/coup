// T15: Center action display box

import { OptimizedBuffer, RGBA } from "@opentui/core";
import { COLORS } from "../constants.js";

const col = (hex: string) => RGBA.fromHex(hex);

// Dynamically sized center box that shrinks on small screens
export function renderCenter(
  buffer: OptimizedBuffer,
  text: string[],
  cx: number,
  cy: number,
  maxWidth?: number,
  maxHeight?: number,
): void {
  const boxW = Math.min(maxWidth ?? 38, 38);
  const boxH = Math.min(maxHeight ?? 7, 7);

  const bx = cx - Math.floor(boxW / 2);
  const by = cy - Math.floor(boxH / 2);

  // Draw bordered box
  buffer.drawBox({
    x: bx,
    y: by,
    width: boxW,
    height: boxH,
    border: true,
    borderColor: col(COLORS.border),
    backgroundColor: col(COLORS.bg),
    shouldFill: true,
  });

  // Word-wrap and draw each text line centered within the box
  const innerWidth = boxW - 2;
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

  const maxLines = boxH - 2;
  const lines = wrapped.slice(0, maxLines);
  const startY = by + 1 + Math.floor((boxH - 2 - lines.length) / 2);

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const tx = bx + 1 + Math.floor((innerWidth - line.length) / 2);
    buffer.drawText(line, tx, startY + i, col(COLORS.textDefault));
  }
}
