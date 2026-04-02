// T18: Action panel — grid layout with cursor navigation

import { RGBA } from "@opentui/core";
import type { OptimizedBuffer } from "@opentui/core";
import { COLORS } from "../constants.js";
import { Action, Phase, actionCategory, CARD_NAMES, CardType, type GameSnapshot } from "@coup/game-client";

const GRID_COLS = 3;

export interface ActionOption {
  label: string;
  action: number;
}

export function getActionOptions(snapshot: GameSnapshot, humanSeat: number): ActionOption[] {
  const mask = snapshot.validMask;
  const options: ActionOption[] = [];
  const phase = snapshot.phase;

  if (phase === Phase.MainAction) {
    // Show action categories, not individual targets
    const categories = new Set<string>();
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      const cat = actionCategory(i);
      if (!categories.has(cat)) {
        categories.add(cat);
        // For targeted actions, just show category once
        if (i >= 4 && i <= 9) options.push({ label: "Coup", action: -1 }); // sentinel
        else if (i >= 10 && i <= 15) options.push({ label: "Steal", action: -2 });
        else if (i >= 16 && i <= 21) options.push({ label: "Assassinate", action: -3 });
        else options.push({ label: cat, action: i });
      }
    }
  } else if (phase === Phase.ChallengeAction || phase === Phase.ChallengeBlock) {
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      if (i === Action.Challenge) options.push({ label: "Challenge", action: i });
      else if (i === Action.Pass) options.push({ label: "Pass", action: i });
      else if (i >= Action.BlockContessa && i <= Action.BlockDuke) {
        const card = blockToCardType(i);
        options.push({ label: `Block (${card ? CARD_NAMES[card] : "?"})`, action: i });
      }
    }
  } else if (phase === Phase.Block) {
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      if (i >= Action.BlockContessa && i <= Action.BlockDuke) {
        const card = blockToCardType(i);
        options.push({ label: `Block (${card ? CARD_NAMES[card] : "?"})`, action: i });
      } else if (i === Action.Pass) options.push({ label: "Pass", action: i });
    }
  } else if (phase === Phase.LoseCard) {
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      if (i >= Action.DiscardSlot0 && i <= Action.DiscardSlot3) {
        const slot = i - Action.DiscardSlot0;
        const player = snapshot.players[humanSeat];
        if (player && slot < 2 && player.cards[slot]) {
          const card = player.cards[slot];
          options.push({ label: CARD_NAMES[card.type], action: i });
        } else {
          options.push({ label: `Slot ${slot}`, action: i });
        }
      }
    }
  } else if (phase === Phase.ExchangeDiscard) {
    const player = snapshot.players[humanSeat];
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      if (i >= Action.DiscardSlot0 && i <= Action.DiscardSlot3) {
        const slot = i - Action.DiscardSlot0;
        let label: string;
        if (slot < 2 && player?.cards[slot]) {
          const cardName = CARD_NAMES[player.cards[slot].type];
          label = `${cardName} (yours)`;
        } else if (snapshot.exchangeCards) {
          const exIdx = slot - 2;
          const cardName = exIdx >= 0 && exIdx < 2 ? CARD_NAMES[snapshot.exchangeCards[exIdx]] : `Card ${slot + 1}`;
          label = `${cardName} (drawn)`;
        } else {
          label = `Card ${slot + 1} (${slot < 2 ? "yours" : "drawn"})`;
        }
        options.push({ label, action: i });
      }
    }
  } else {
    // Fallback: show all valid actions
    for (let i = 0; i < 32; i++) {
      if (!((mask >> i) & 1)) continue;
      options.push({ label: actionCategory(i), action: i });
    }
  }

  return options;
}

export function getTargetOptions(
  snapshot: GameSnapshot,
  actionType: "coup" | "steal" | "assassinate",
  humanSeat: number,
): ActionOption[] {
  const mask = snapshot.validMask;
  const options: ActionOption[] = [];
  let base: number;
  switch (actionType) {
    case "coup": base = Action.CoupP0; break;
    case "steal": base = Action.StealP0; break;
    case "assassinate": base = Action.AssassinateP0; break;
  }

  for (let i = 0; i < 6; i++) {
    const action = base + i;
    if (!((mask >> action) & 1)) continue;
    const player = snapshot.players[i];
    if (!player) continue;
    const name = i === humanSeat ? "You" : `bot-${i}`;
    options.push({
      label: `${name} (${player.coins}●, ${player.influence} inf)`,
      action,
    });
  }

  return options;
}

export function renderActionPanel(
  buffer: OptimizedBuffer,
  options: ActionOption[],
  selectedIndex: number,
  headerText: string,
  x: number,
  y: number,
  width: number,
  height: number,
  showEsc: boolean = false,
) {
  const bg = RGBA.fromHex(COLORS.bg);
  const textColor = RGBA.fromHex(COLORS.textDefault);
  const dimColor = RGBA.fromHex(COLORS.textDim);
  const brightColor = RGBA.fromHex(COLORS.textBright);
  const cursorColor = RGBA.fromHex(COLORS.cursor);

  // Fill background
  buffer.fillRect(x, y, width, height, bg);

  // Draw separator line
  const sepLine = "═".repeat(width);
  buffer.drawText(sepLine, x, y, RGBA.fromHex(COLORS.border), bg);

  // Hints
  const hints: string[] = [];
  if (showEsc) hints.push("ESC back");
  hints.push("(H) history");
  const hintText = hints.join("  ");
  buffer.drawText(hintText, x + width - hintText.length - 2, y + 1, dimColor, bg);

  // Header (truncate to fit, leave room for hints)
  const maxHeaderLen = width - hintText.length - 6;
  const displayHeader = headerText.length > maxHeaderLen
    ? headerText.slice(0, maxHeaderLen - 1) + "…"
    : headerText;
  buffer.drawText(displayHeader, x + 2, y + 1, textColor, bg);

  // Options in grid
  const startY = y + 3;
  const colWidth = Math.floor((width - 4) / GRID_COLS);

  const selBg = RGBA.fromHex(COLORS.selectionBg);

  for (let i = 0; i < options.length; i++) {
    const row = Math.floor(i / GRID_COLS);
    const col = i % GRID_COLS;
    const ox = x + 2 + col * colWidth;
    const oy = startY + row;

    if (oy >= y + height) break;

    const isSelected = i === selectedIndex;
    const prefix = isSelected ? "▸ " : "  ";
    const fg = isSelected ? brightColor : dimColor;
    const rowBg = isSelected ? selBg : bg;

    const text = prefix + options[i].label;
    if (isSelected) {
      buffer.fillRect(ox, oy, text.length + 1, 1, selBg);
    }
    buffer.drawText(text, ox, oy, fg, rowBg);
  }
}

function blockToCardType(action: number): CardType | null {
  switch (action) {
    case Action.BlockContessa: return CardType.Contessa;
    case Action.BlockCaptain: return CardType.Captain;
    case Action.BlockAmbassador: return CardType.Ambassador;
    case Action.BlockDuke: return CardType.Duke;
    default: return null;
  }
}

// Grid navigation helpers
export function navigateGrid(
  currentIndex: number,
  direction: "up" | "down" | "left" | "right",
  totalItems: number,
  cols: number = GRID_COLS,
): number {
  const row = Math.floor(currentIndex / cols);
  const col = currentIndex % cols;
  const totalRows = Math.ceil(totalItems / cols);

  switch (direction) {
    case "left": {
      const next = currentIndex - 1;
      return next < 0 ? totalItems - 1 : next;
    }
    case "right": {
      const next = currentIndex + 1;
      return next >= totalItems ? 0 : next;
    }
    case "up": {
      const newRow = row > 0 ? row - 1 : totalRows - 1;
      let newIndex = newRow * cols + col;
      if (newIndex >= totalItems) newIndex = totalItems - 1;
      return Math.max(0, newIndex);
    }
    case "down": {
      const newRow = row < totalRows - 1 ? row + 1 : 0;
      let newIndex = newRow * cols + col;
      if (newIndex >= totalItems) newIndex = totalItems - 1;
      return Math.max(0, newIndex);
    }
  }
}
