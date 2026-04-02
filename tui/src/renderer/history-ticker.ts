// T19: History ticker — 3-line scrolling area + full history overlay

import { RGBA } from "@opentui/core";
import type { OptimizedBuffer } from "@opentui/core";
import { COLORS } from "../constants.js";

export class HistoryTicker {
  private entries: string[] = [];
  private maxLines: number;

  constructor(maxLines: number = 3) {
    this.maxLines = maxLines;
  }

  push(text: string) {
    this.entries.push(text);
  }

  clear() {
    this.entries = [];
  }

  render(
    buffer: OptimizedBuffer,
    x: number,
    y: number,
    width: number,
    height: number,
  ) {
    const bg = RGBA.fromHex(COLORS.bg);
    const borderColor = RGBA.fromHex(COLORS.border);
    const textColor = RGBA.fromHex(COLORS.textDim);
    const brightColor = RGBA.fromHex(COLORS.textDefault);

    buffer.fillRect(x, y, width, height, bg);
    buffer.drawText("═".repeat(width), x, y, borderColor, bg);

    const visible = this.entries.slice(-this.maxLines);
    for (let i = 0; i < visible.length; i++) {
      const isLatest = i === visible.length - 1;
      const fg = isLatest ? brightColor : textColor;
      const text = visible[i].slice(0, width - 2);
      buffer.drawText(text, x + 1, y + 1 + i, fg, bg);
    }
  }

  renderFullHistory(
    buffer: OptimizedBuffer,
    scrollOffset: number,
    width: number,
    height: number,
  ) {
    const bg = RGBA.fromHex(COLORS.bg);
    const borderColor = RGBA.fromHex(COLORS.border);
    const textColor = RGBA.fromHex(COLORS.textDefault);
    const dimColor = RGBA.fromHex(COLORS.textDim);
    const brightColor = RGBA.fromHex(COLORS.textBright);

    buffer.clear(bg);

    // Title bar
    const title = " History (↑↓ scroll, C copy, H/ESC close) ";
    buffer.drawText("═".repeat(width), 0, 0, borderColor, bg);
    buffer.drawText(title, Math.floor((width - title.length) / 2), 0, brightColor, bg);

    // Content area
    const contentH = height - 3; // title + bottom border + hint
    const total = this.entries.length;
    const maxScroll = Math.max(0, total - contentH);
    const offset = Math.min(Math.max(0, scrollOffset), maxScroll);

    for (let i = 0; i < contentH && offset + i < total; i++) {
      const entryIdx = offset + i;
      const lineNum = `${(entryIdx + 1).toString().padStart(3)} `;
      const text = this.entries[entryIdx].slice(0, width - 6);
      buffer.drawText(lineNum, 1, 1 + i, dimColor, bg);
      buffer.drawText(text, 5, 1 + i, textColor, bg);
    }

    if (total === 0) {
      buffer.drawText("No history yet.", 5, 2, dimColor, bg);
    }

    // Bottom bar
    buffer.drawText("═".repeat(width), 0, height - 2, borderColor, bg);
    const statusText = `${total} events | showing ${offset + 1}-${Math.min(offset + contentH, total)}`;
    buffer.drawText(statusText, 2, height - 1, dimColor, bg);
  }

  get maxScrollOffset(): number {
    return Math.max(0, this.entries.length - 20); // approximate
  }

  get length(): number {
    return this.entries.length;
  }

  getAllEntries(): string[] {
    return this.entries.slice();
  }
}
