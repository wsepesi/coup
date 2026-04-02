// T20: Setup screen — interactive config

import { RGBA } from "@opentui/core";
import type { OptimizedBuffer } from "@opentui/core";
import { COLORS } from "./constants.js";

export interface GameConfig {
  players: number;
  seat: number;
  difficulty: "easy" | "medium" | "hard";
  seed: string;
}

interface SetupField {
  label: string;
  value: string;
  options?: string[];
  min?: number;
  max?: number;
  isInput?: boolean;
}

export class SetupScreen {
  private fields: SetupField[];
  private selectedField: number = 0;
  private config: GameConfig;

  constructor() {
    this.config = { players: 4, seat: 0, difficulty: "medium", seed: "" };
    this.fields = [
      { label: "Players", value: "4", min: 2, max: 6 },
      { label: "Your seat", value: "0", min: 0, max: 3 },
      { label: "Bot level", value: "Medium", options: ["Easy", "Medium", "Hard"] },
      { label: "Seed", value: "", isInput: true },
    ];
  }

  handleKey(name: string): "start" | null {
    switch (name) {
      case "up":
        this.selectedField = Math.max(0, this.selectedField - 1);
        break;
      case "down":
        this.selectedField = Math.min(this.fields.length, this.selectedField + 1);
        break;
      case "left":
        this.adjustField(-1);
        break;
      case "right":
        this.adjustField(1);
        break;
      case "return":
        if (this.selectedField === this.fields.length) {
          return "start";
        }
        break;
      case "backspace":
        if (this.fields[this.selectedField]?.isInput) {
          this.fields[this.selectedField].value = this.fields[this.selectedField].value.slice(0, -1);
        }
        break;
      default:
        if (this.fields[this.selectedField]?.isInput && /^\d$/.test(name)) {
          this.fields[this.selectedField].value += name;
        }
        break;
    }
    this.syncConfig();
    return null;
  }

  private adjustField(dir: number) {
    const field = this.fields[this.selectedField];
    if (!field) return;

    if (field.options) {
      const idx = field.options.findIndex(o => o.toLowerCase() === field.value.toLowerCase());
      const newIdx = Math.max(0, Math.min(field.options.length - 1, idx + dir));
      field.value = field.options[newIdx];
    } else if (field.min != null && field.max != null && !field.isInput) {
      const val = parseInt(field.value) || field.min;
      const newVal = Math.max(field.min, Math.min(field.max, val + dir));
      field.value = String(newVal);
      // Update seat max when player count changes
      if (field.label === "Players") {
        const seatField = this.fields[1];
        seatField.max = newVal - 1;
        if (parseInt(seatField.value) >= newVal) {
          seatField.value = String(newVal - 1);
        }
      }
    }
  }

  private syncConfig() {
    this.config.players = parseInt(this.fields[0].value) || 4;
    this.config.seat = parseInt(this.fields[1].value) || 0;
    this.config.difficulty = (this.fields[2].value.toLowerCase() as "easy" | "medium" | "hard");
    this.config.seed = this.fields[3].value;
  }

  getConfig(): GameConfig {
    return { ...this.config };
  }

  render(buffer: OptimizedBuffer, width: number, height: number) {
    const bg = RGBA.fromHex(COLORS.bg);
    const bright = RGBA.fromHex(COLORS.textBright);
    const textColor = RGBA.fromHex(COLORS.textDefault);
    const dim = RGBA.fromHex(COLORS.textDim);
    const border = RGBA.fromHex(COLORS.border);
    const cursor = RGBA.fromHex(COLORS.cursor);

    buffer.clear(bg);

    const boxW = 50;
    const boxH = 14;
    const bx = Math.floor((width - boxW) / 2);
    const by = Math.floor((height - boxH) / 2);

    // Draw outer box
    buffer.drawBox({
      x: bx, y: by, width: boxW, height: boxH,
      border: true, borderColor: border, backgroundColor: bg,
      borderStyle: "double", shouldFill: true,
    });

    // Title
    const title = "C O U P";
    buffer.drawText(title, bx + Math.floor((boxW - title.length) / 2), by + 2, bright, bg);
    buffer.drawText("─────", bx + Math.floor((boxW - 5) / 2), by + 3, dim, bg);

    // Fields
    for (let i = 0; i < this.fields.length; i++) {
      const field = this.fields[i];
      const fy = by + 5 + i;
      const isSelected = i === this.selectedField;
      const fg = isSelected ? bright : textColor;

      buffer.drawText(`${field.label}:`, bx + 4, fy, fg, bg);

      if (field.isInput) {
        const val = field.value || "random";
        buffer.drawText(`[ ${val.padEnd(10)} ]`, bx + 16, fy, isSelected ? cursor : dim, bg);
      } else if (field.options) {
        buffer.drawText(`◀ ${field.value.padEnd(8)} ▶`, bx + 16, fy, isSelected ? cursor : dim, bg);
      } else {
        buffer.drawText(`◀ ${field.value} ▶`, bx + 16, fy, isSelected ? cursor : dim, bg);
      }
    }

    // Start button
    const btnY = by + 5 + this.fields.length + 1;
    const isStartSelected = this.selectedField === this.fields.length;
    const btnText = "[ Start Game ]";
    buffer.drawText(
      btnText,
      bx + Math.floor((boxW - btnText.length) / 2),
      btnY,
      isStartSelected ? cursor : textColor,
      bg,
    );

    // Help text
    buffer.drawText(
      "↑↓ navigate  ◀▶ adjust  Enter confirm",
      bx + 4, by + boxH - 2, dim, bg,
    );
  }
}
