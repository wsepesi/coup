"use client";

import { useState, useEffect, useLayoutEffect, useCallback } from "react";
import type { ActionInfo, PlayerInfo, CardInfo } from "@/lib/types";
import { useKeyboard } from "@/hooks/useKeyboard";
import { ROLE_NAMES } from "@/lib/constants";

interface ActionPickerProps {
  actions: ActionInfo[];
  onAction: (action: number) => void;
  players?: PlayerInfo[];
  phase?: string;
  yourCards?: CardInfo[];
  onTargetHover?: (seat: number | null) => void;
}

const COLS = 3;

// Targeted action ranges (matching the 32-action space)
const COUP_BASE = 4;
const STEAL_BASE = 10;
const ASSASSINATE_BASE = 16;
const DISCARD_SLOT0 = 28;

interface MenuOption {
  label: string;
  action: number; // real action ID, or -1/-2/-3 for category sentinels
}

function isTargetedAction(id: number): boolean {
  return (id >= COUP_BASE && id <= 9) || (id >= STEAL_BASE && id <= 15) || (id >= ASSASSINATE_BASE && id <= 21);
}

function categoryForAction(id: number): "coup" | "steal" | "assassinate" | null {
  if (id >= COUP_BASE && id <= 9) return "coup";
  if (id >= STEAL_BASE && id <= 15) return "steal";
  if (id >= ASSASSINATE_BASE && id <= 21) return "assassinate";
  return null;
}

const CATEGORY_LABELS: Record<string, string> = {
  coup: "Coup",
  steal: "Steal",
  assassinate: "Assassinate",
};
const CATEGORY_SENTINELS: Record<string, number> = {
  coup: -1,
  steal: -2,
  assassinate: -3,
};

// Clean labels for the main action category menu (strip role hints like "(Duke)")
const CLEAN_LABELS: Record<number, string> = {
  0: "Income",
  1: "Foreign Aid",
  2: "Tax",
  3: "Exchange",
};

function buildCategoryMenu(actions: ActionInfo[]): MenuOption[] {
  const options: MenuOption[] = [];
  const seenCategories = new Set<string>();

  for (const a of actions) {
    const cat = categoryForAction(a.id);
    if (cat && !seenCategories.has(cat)) {
      seenCategories.add(cat);
      options.push({ label: CATEGORY_LABELS[cat], action: CATEGORY_SENTINELS[cat] });
    } else if (!cat) {
      options.push({ label: CLEAN_LABELS[a.id] ?? a.label, action: a.id });
    }
  }
  return options;
}

function buildTargetMenu(actions: ActionInfo[], category: string, players?: PlayerInfo[]): MenuOption[] {
  const base = category === "coup" ? COUP_BASE : category === "steal" ? STEAL_BASE : ASSASSINATE_BASE;
  return actions
    .filter((a) => {
      const cat = categoryForAction(a.id);
      return cat === category;
    })
    .map((a) => {
      const targetIdx = a.id - base;
      const player = players?.[targetIdx];
      const name = player?.name ?? `Player ${targetIdx}`;
      const coins = player?.coins ?? 0;
      const inf = player?.influence ?? 0;
      return {
        label: `${name} (${coins}\u25CF, ${inf} inf)`,
        action: a.id,
      };
    });
}

function navigateGrid(current: number, direction: string, total: number): number {
  const rows = Math.ceil(total / COLS);
  let col = current % COLS;
  let row = Math.floor(current / COLS);

  switch (direction) {
    case "ArrowLeft":
      col = col > 0 ? col - 1 : Math.min(COLS - 1, total - row * COLS - 1);
      break;
    case "ArrowRight":
      col = col < Math.min(COLS - 1, total - row * COLS - 1) ? col + 1 : 0;
      break;
    case "ArrowUp":
      row = row > 0 ? row - 1 : rows - 1;
      break;
    case "ArrowDown":
      row = row < rows - 1 ? row + 1 : 0;
      break;
  }

  const idx = row * COLS + col;
  return idx < total ? idx : total - 1;
}

function enrichExchangeLabel(action: ActionInfo, yourCards?: CardInfo[]): string {
  const slot = action.id - DISCARD_SLOT0;
  if (slot < 0 || slot > 3 || !yourCards || slot >= yourCards.length) return action.label;
  const card = yourCards[slot];
  const name = ROLE_NAMES[card.type] ?? "???";
  const source = slot < 2 ? "(yours)" : "(drawn)";
  return `${name} ${source}`;
}

export default function ActionPicker({ actions, onAction, players, phase, yourCards, onTargetHover }: ActionPickerProps) {
  const [selected, setSelected] = useState(0);
  const [showCursor, setShowCursor] = useState(false);
  const [activeCategory, setActiveCategory] = useState<string | null>(null);

  // Determine if we should show two-step menu (only in main action phase with targeted actions)
  const isMainAction = phase === "action";
  const hasTargeted = actions.some((a) => isTargetedAction(a.id));
  const useTwoStep = isMainAction && hasTargeted;
  // Build current menu options
  const options: MenuOption[] = useTwoStep
    ? activeCategory
      ? buildTargetMenu(actions, activeCategory, players)
      : buildCategoryMenu(actions)
    : phase === "exchange_discard"
      ? actions.map((a) => ({ label: enrichExchangeLabel(a, yourCards), action: a.id }))
      : actions.map((a) => ({ label: a.label, action: a.id }));

  // Reset cursor when actions/phase change — default to Pass in reactive phases
  // useLayoutEffect prevents a flash of the wrong selection before paint
  useLayoutEffect(() => {
    setActiveCategory(null);
    setShowCursor(false);
    if (!isMainAction) {
      // Find Pass in the raw actions list (action ID 23)
      const passIdx = actions.findIndex((a) => a.id === 23);
      setSelected(passIdx >= 0 ? passIdx : 0);
    } else {
      setSelected(0);
    }
  }, [actions, isMainAction, phase]);

  // Notify parent of hovered target seat
  useEffect(() => {
    if (!onTargetHover) return;
    if (activeCategory && options[selected]) {
      const actionId = options[selected].action;
      if (actionId >= COUP_BASE && actionId <= 9) { onTargetHover(actionId - COUP_BASE); return; }
      if (actionId >= STEAL_BASE && actionId <= 15) { onTargetHover(actionId - STEAL_BASE); return; }
      if (actionId >= ASSASSINATE_BASE && actionId <= 21) { onTargetHover(actionId - ASSASSINATE_BASE); return; }
    }
    onTargetHover(null);
  }, [activeCategory, selected, options, onTargetHover]);

  const handleSelect = useCallback(
    (opt: MenuOption) => {
      if (opt.action === -1 || opt.action === -2 || opt.action === -3) {
        // Category sentinel → enter target sub-menu
        const cat = opt.action === -1 ? "coup" : opt.action === -2 ? "steal" : "assassinate";
        setActiveCategory(cat);
        setSelected(0);
        setShowCursor(false);
      } else {
        onAction(opt.action);
      }
    },
    [onAction],
  );

  const confirm = useCallback(() => {
    if (options[selected]) handleSelect(options[selected]);
  }, [options, selected, handleSelect]);

  useKeyboard(
    {
      ArrowLeft: () => { setShowCursor(true); setSelected((s) => navigateGrid(s, "ArrowLeft", options.length)); },
      ArrowRight: () => { setShowCursor(true); setSelected((s) => navigateGrid(s, "ArrowRight", options.length)); },
      ArrowUp: () => { setShowCursor(true); setSelected((s) => navigateGrid(s, "ArrowUp", options.length)); },
      ArrowDown: () => { setShowCursor(true); setSelected((s) => navigateGrid(s, "ArrowDown", options.length)); },
      Enter: confirm,
      Escape: () => { if (activeCategory) { setActiveCategory(null); setSelected(0); setShowCursor(false); } },
      "1": () => { if (options[0]) handleSelect(options[0]); },
      "2": () => { if (options[1]) handleSelect(options[1]); },
      "3": () => { if (options[2]) handleSelect(options[2]); },
      "4": () => { if (options[3]) handleSelect(options[3]); },
      "5": () => { if (options[4]) handleSelect(options[4]); },
      "6": () => { if (options[5]) handleSelect(options[5]); },
      "7": () => { if (options[6]) handleSelect(options[6]); },
      "8": () => { if (options[7]) handleSelect(options[7]); },
      "9": () => { if (options[8]) handleSelect(options[8]); },
    },
    [options, selected, confirm, activeCategory, handleSelect],
    options.length > 0,
  );

  if (options.length === 0) return null;

  const header = activeCategory
    ? `${CATEGORY_LABELS[activeCategory]} \u2192 choose target:`
    : undefined;

  return (
    <div className="border-t border-border-term pt-3">
      <div className="flex items-center justify-between text-text-dim text-xs mb-2">
        <span>{"═".repeat(30)}</span>
        <span>{activeCategory ? "ESC back" : ""} (H) history</span>
      </div>
      {header && (
        <div className="text-text-bright text-sm mb-1">{header}</div>
      )}
      <div className="grid grid-cols-3 gap-1">
        {options.map((opt, i) => {
          const isSelected = i === selected;
          return (
            <button
              key={opt.action}
              onClick={() => handleSelect(opt)}
              className={`text-left text-sm px-2 py-1 transition-colors ${
                isSelected
                  ? "bg-selection-bg text-text-bright"
                  : "text-text-dim hover:bg-selection-bg hover:text-text-bright"
              }`}
            >
              <span className={isSelected ? "text-cursor mr-1" : "mr-1 opacity-0"}>▸</span>
              <span className="text-text-dim mr-1 text-xs">{i + 1}.</span>
              {opt.label}
            </button>
          );
        })}
      </div>
    </div>
  );
}
