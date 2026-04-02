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
    this.config = { players: 6, seat: 0, difficulty: "medium", seed: "" };
    this.fields = [
      { label: "Players", value: "6", min: 2, max: 6 },
      { label: "Your seat", value: "Random", options: this.buildSeatOptions(6) },
      { label: "Bot level", value: "Medium", options: ["Easy", "Medium", "Hard"] },
      { label: "Seed", value: "", isInput: true },
    ];
  }

  private buildSeatOptions(numPlayers: number): string[] {
    const opts = ["Random", "Spectator"];
    for (let i = 0; i < numPlayers; i++) opts.push(String(i));
    return opts;
  }

  handleKey(name: string): "start" | "rules" | null {
    switch (name) {
      case "r":
        return "rules";
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
      let newVal = val + dir;
      if (newVal > field.max) newVal = field.min;
      else if (newVal < field.min) newVal = field.max;
      field.value = String(newVal);
      // Update seat options when player count changes
      if (field.label === "Players") {
        const seatField = this.fields[1];
        seatField.options = this.buildSeatOptions(newVal);
        // If current seat value is a number >= new player count, reset
        const seatNum = parseInt(seatField.value);
        if (!isNaN(seatNum) && seatNum >= newVal) {
          seatField.value = String(newVal - 1);
        }
      }
    }
  }

  private syncConfig() {
    this.config.players = parseInt(this.fields[0].value) || 4;
    const seatVal = this.fields[1].value;
    if (seatVal === "Spectator") {
      this.config.seat = -1;
    } else if (seatVal === "Random") {
      this.config.seat = -2; // resolved at game start
    } else {
      this.config.seat = parseInt(seatVal) || 0;
    }
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

    const selBg = RGBA.fromHex(COLORS.selectionBg);

    // Fields
    for (let i = 0; i < this.fields.length; i++) {
      const field = this.fields[i];
      const fy = by + 5 + i;
      const isSelected = i === this.selectedField;
      const rowBg = isSelected ? selBg : bg;
      const fg = isSelected ? bright : textColor;

      // Fill row background for selected highlight
      if (isSelected) {
        buffer.fillRect(bx + 2, fy, boxW - 4, 1, selBg);
      }

      buffer.drawText(`${field.label}:`, bx + 4, fy, fg, rowBg);

      if (field.isInput) {
        const val = field.value || "random";
        buffer.drawText(`[ ${val.padEnd(10)} ]`, bx + 16, fy, isSelected ? cursor : dim, rowBg);
      } else if (field.options) {
        buffer.drawText(`◀ ${field.value.padEnd(8)} ▶`, bx + 16, fy, isSelected ? cursor : dim, rowBg);
      } else {
        buffer.drawText(`◀ ${field.value} ▶`, bx + 16, fy, isSelected ? cursor : dim, rowBg);
      }
    }

    // Start button
    const btnY = by + 5 + this.fields.length + 1;
    const isStartSelected = this.selectedField === this.fields.length;
    const btnText = "[ Start Game ]";
    const btnBg = isStartSelected ? selBg : bg;
    if (isStartSelected) {
      buffer.fillRect(bx + 2, btnY, boxW - 4, 1, selBg);
    }
    buffer.drawText(
      btnText,
      bx + Math.floor((boxW - btnText.length) / 2),
      btnY,
      isStartSelected ? cursor : textColor,
      btnBg,
    );

    // Help text
    const help = "↑↓ ◀▶ navigate  Enter confirm  (R) rules";
    buffer.drawText(help, bx + Math.floor((boxW - help.length) / 2), by + boxH - 2, dim, bg);
  }
}

// ── Rules screen ──────────────────────────────────────────────────────

const RULES_LINES: string[] = [
  "",
  "C O U P",
  "Rules & Guide",
  "",
  "",
  "OVERVIEW",
  "────────────────────────────────────────────────────────────────────────",
  "Coup is a game of bluffing and deduction for 2-6 players. Each player starts with 2 influence",
  "cards (face down) and 2 coins. Last player with influence wins.",
  "",
  "You can claim ANY role for your action, even if you don't have it — but if someone challenges",
  "you and you're bluffing, you lose a card.",
  "",
  "",
  "THE 5 ROLES",
  "────────────────────────────────────────────────────────────────────────",
  "",
  "  ♦ DUKE",
  "    Action: Tax — take 3 coins from the treasury",
  "    Blocks: Foreign Aid",
  "",
  "  † ASSASSIN",
  "    Action: Assassinate — pay 3 coins, target loses influence",
  "    Blocked by: Contessa",
  "",
  "  ⚓ CAPTAIN",
  "    Action: Steal — take 2 coins from another player",
  "    Blocks: Stealing",
  "    Blocked by: Captain, Ambassador",
  "",
  "  ✦ AMBASSADOR",
  "    Action: Exchange — draw 2 cards from the deck, return 2",
  "    Blocks: Stealing",
  "",
  "  ♥ CONTESSA",
  "    Action: None",
  "    Blocks: Assassination",
  "",
  "",
  "GENERAL ACTIONS (no role needed)",
  "────────────────────────────────────────────────────────────────────────",
  "",
  "  Income        Take 1 coin. Cannot be blocked or challenged.",
  "",
  "  Foreign Aid   Take 2 coins. Can be blocked by Duke.",
  "",
  "  Coup          Pay 7 coins, target loses influence. Mandatory at 10+ coins. Cannot be blocked.",
  "",
  "",
  "CHALLENGES",
  "────────────────────────────────────────────────────────────────────────",
  "When a player claims a role, any other player may challenge. If challenged:",
  "",
  "  - Bluffing?   Challenger wins. The bluffer loses an influence card.",
  "",
  "  - Truthful?   Challenger loses. The claimant reveals the card, shuffles it back, draws a new one.",
  "",
  "Example: bot-2 claims Duke for Tax. You challenge. bot-2 doesn't have Duke — bot-2 loses a card!",
  "",
  "Example: You claim Captain to Steal from bot-1. bot-1 challenges. You reveal Captain — bot-1 loses",
  "  a card. You shuffle Captain back, draw a replacement, then the Steal resolves.",
  "",
  "",
  "BLOCKING",
  "────────────────────────────────────────────────────────────────────────",
  "Some actions can be blocked by specific roles. The blocker claims to have that role.",
  "The original actor (or anyone) can then challenge the block.",
  "",
  "Example: You take Foreign Aid. bot-3 claims Duke to block. You can challenge or let it stand.",
  "",
  "Example: bot-1 Steals from you. You block with Captain. bot-1 doesn't challenge — steal prevented.",
  "",
  "",
  "LOSING INFLUENCE",
  "────────────────────────────────────────────────────────────────────────",
  "When you lose influence, you choose which of your face-down cards to reveal.",
  "That card is turned face-up permanently. A player with no face-down cards is eliminated.",
  "",
  "",
  "STRATEGY TIPS",
  "────────────────────────────────────────────────────────────────────────",
  "- Bluffing Duke early is strong — Tax gives 3 coins and Duke blocks Foreign Aid.",
  "- Avoid Foreign Aid when Dukes may be alive — it just gets blocked.",
  "- Challenge more when you hold cards of the claimed type — fewer copies left, higher bluff chance.",
  "- At 7+ coins, Coup is often better than role actions — it can't be blocked or challenged.",
  "- Watch what others claim. If two players both claim Duke, one is likely bluffing.",
  "",
];

export async function showRulesScreen(
  buffer: OptimizedBuffer,
  waitForKey: () => Promise<{ name: string }>,
  width: number,
  height: number,
): Promise<void> {
  const bg = RGBA.fromHex(COLORS.bg);
  const bright = RGBA.fromHex(COLORS.textBright);
  const textColor = RGBA.fromHex(COLORS.textDefault);
  const dim = RGBA.fromHex(COLORS.textDim);
  const border = RGBA.fromHex(COLORS.border);

  const contentH = height - 3;
  const total = RULES_LINES.length;
  const maxScroll = Math.max(0, total - contentH);
  let scrollOffset = 0;

  // Track which role color to use for sub-lines (Action/Blocks/Blocked by)
  const sectionHeaders = new Set([
    "OVERVIEW", "THE 5 ROLES", "GENERAL ACTIONS (no role needed)",
    "CHALLENGES", "BLOCKING", "LOSING INFLUENCE", "STRATEGY TIPS",
  ]);

  function lineColor(line: string, prevRoleColor: RGBA | null): { fg: RGBA; roleColor: RGBA | null } {
    // Border lines
    if (line.startsWith("═")) return { fg: border, roleColor: prevRoleColor };
    // Separator dashes
    if (/^─/.test(line)) return { fg: dim, roleColor: prevRoleColor };

    // Section headers — bright
    const trimmed = line.trim();
    if (trimmed === "C O U P" || trimmed === "Rules & Guide")
      return { fg: bright, roleColor: null };
    if (sectionHeaders.has(trimmed)) return { fg: bright, roleColor: null };

    // Role headers — card color, set context for sub-lines
    if (line.startsWith("  ♦ DUKE")) return { fg: RGBA.fromHex(COLORS.duke), roleColor: RGBA.fromHex(COLORS.duke) };
    if (line.startsWith("  † ASSASSIN")) return { fg: RGBA.fromHex(COLORS.assassin), roleColor: RGBA.fromHex(COLORS.assassin) };
    if (line.startsWith("  ⚓ CAPTAIN")) return { fg: RGBA.fromHex(COLORS.captain), roleColor: RGBA.fromHex(COLORS.captain) };
    if (line.startsWith("  ✦ AMBASSADOR")) return { fg: RGBA.fromHex(COLORS.ambassador), roleColor: RGBA.fromHex(COLORS.ambassador) };
    if (line.startsWith("  ♥ CONTESSA")) return { fg: RGBA.fromHex(COLORS.contessa), roleColor: RGBA.fromHex(COLORS.contessa) };

    // Role detail lines — inherit role color but dimmer (use dim)
    if (prevRoleColor && /^\s{4}(Action|Blocks|Blocked by):/.test(line))
      return { fg: prevRoleColor, roleColor: prevRoleColor };
    // Continuation lines under role details
    if (prevRoleColor && /^\s{6}/.test(line))
      return { fg: dim, roleColor: prevRoleColor };

    // Examples — dim italic feel
    if (/^\s*Example:/.test(line)) return { fg: dim, roleColor: null };
    // Example continuation
    if (/^\s{2}\S/.test(line) && prevRoleColor === null) return { fg: dim, roleColor: null };

    // General action names (Income, Foreign Aid, Coup) — bright
    if (/^\s{2}(Income|Foreign Aid|Coup)\s/.test(line)) return { fg: bright, roleColor: null };

    // Bullet points in strategy — default
    if (line.startsWith("- ")) return { fg: textColor, roleColor: null };

    return { fg: textColor, roleColor: prevRoleColor };
  }

  function render() {
    buffer.clear(bg);

    // Title bar
    const title = " Rules (↑↓ scroll, R/ESC close) ";
    buffer.drawText("═".repeat(width), 0, 0, border, bg);
    buffer.drawText(title, Math.floor((width - title.length) / 2), 0, bright, bg);

    // We need role color context from the first visible line, so scan from start
    let roleCtx: RGBA | null = null;
    for (let j = 0; j < scrollOffset && j < total; j++) {
      const { roleColor } = lineColor(RULES_LINES[j], roleCtx);
      roleCtx = roleColor;
    }

    const lx = Math.max(2, Math.floor(width * 0.05));
    const maxLineW = width - lx * 2;
    for (let i = 0; i < contentH && scrollOffset + i < total; i++) {
      const line = RULES_LINES[scrollOffset + i];
      const displayLine = line.slice(0, maxLineW);

      const { fg, roleColor } = lineColor(line, roleCtx);
      roleCtx = roleColor;

      // Center title lines
      const trimmedLine = line.trim();
      const isTitle = trimmedLine === "C O U P" || trimmedLine === "Rules & Guide";
      const drawX = isTitle ? Math.floor((width - trimmedLine.length) / 2) : lx;
      const drawText = isTitle ? trimmedLine : displayLine;
      buffer.drawText(drawText, drawX, 1 + i, fg, bg);
    }

    // Bottom bar
    buffer.drawText("═".repeat(width), 0, height - 2, border, bg);
    const pct = total <= contentH ? 100 : Math.round(((scrollOffset + contentH) / total) * 100);
    const status = `${Math.min(pct, 100)}%  ↑↓ scroll  R/ESC close`;
    buffer.drawText(status, 2, height - 1, dim, bg);
  }

  render();

  while (true) {
    const key = await waitForKey();
    if (key.name === "r" || key.name === "escape") return;
    if (key.name === "up") scrollOffset = Math.max(0, scrollOffset - 1);
    else if (key.name === "down") scrollOffset = Math.min(maxScroll, scrollOffset + 1);
    else if (key.name === "pageup" || key.name === " ") scrollOffset = Math.max(0, scrollOffset - contentH);
    else if (key.name === "pagedown") scrollOffset = Math.min(maxScroll, scrollOffset + contentH);
    render();
  }
}
