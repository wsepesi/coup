"use client";

import { useState, useEffect, useLayoutEffect, useCallback, useMemo } from "react";
import type { ActionInfo, PlayerInfo, CardInfo } from "@/lib/types";
import { useKeyboard } from "@/hooks/useKeyboard";
import { ROLE_NAMES, ROLE_COLORS, ACTION, actionTarget } from "@/lib/constants";

interface ActionPickerProps {
  actions: ActionInfo[];
  onAction: (action: number) => void;
  players: PlayerInfo[];
  phase: string;
  cards: CardInfo[];
  coins: number;
  disabled?: boolean;
  onTargetHover?: (seat: number | null) => void;
}

type Category = "coup" | "steal" | "assassinate";

interface MenuOption {
  label: string;
  hint?: string;
  action: number | Category;
  tone?: "danger" | "primary" | "block" | "muted";
  color?: string;
}

const CATEGORY_OF = (id: number): Category | null =>
  id >= 4 && id <= 9 ? "coup" : id >= 10 && id <= 15 ? "steal" : id >= 16 && id <= 21 ? "assassinate" : null;

const MAIN_INFO: Record<number, { label: string; hint: string; color?: string }> = {
  [ACTION.INCOME]: { label: "Income", hint: "+1 coin, safe" },
  [ACTION.FOREIGN_AID]: { label: "Foreign Aid", hint: "+2, Duke can block" },
  [ACTION.TAX]: { label: "Tax", hint: "claim Duke, +3", color: ROLE_COLORS.Duke },
  [ACTION.EXCHANGE]: { label: "Exchange", hint: "claim Ambassador", color: ROLE_COLORS.Ambassador },
};
const CATEGORY_INFO: Record<Category, { label: string; hint: string; color?: string }> = {
  coup: { label: "Coup", hint: "pay 7, unstoppable" },
  steal: { label: "Steal", hint: "claim Captain, take 2", color: ROLE_COLORS.Captain },
  assassinate: { label: "Assassinate", hint: "claim Assassin, pay 3", color: ROLE_COLORS.Assassin },
};
const BLOCK_ROLE: Record<number, string> = {
  [ACTION.BLOCK_CONTESSA]: "Contessa",
  [ACTION.BLOCK_CAPTAIN]: "Captain",
  [ACTION.BLOCK_AMBASSADOR]: "Ambassador",
  [ACTION.BLOCK_DUKE]: "Duke",
};

function buildOptions(actions: ActionInfo[], phase: string, category: Category | null, players: PlayerInfo[], cards: CardInfo[]): MenuOption[] {
  if (phase === "action") {
    if (category) {
      return actions
        .filter((a) => CATEGORY_OF(a.id) === category)
        .map((a) => {
          const p = players[actionTarget(a.id)!];
          return { label: p?.name ?? a.label, hint: `${p?.coins ?? 0}● · ${p?.influence ?? 0} card${p?.influence === 1 ? "" : "s"}`, action: a.id, tone: category === "coup" ? "danger" : undefined };
        });
    }
    const out: MenuOption[] = [];
    const seen = new Set<Category>();
    for (const a of actions) {
      const cat = CATEGORY_OF(a.id);
      if (cat) {
        if (seen.has(cat)) continue;
        seen.add(cat);
        out.push({ ...CATEGORY_INFO[cat], action: cat });
      } else {
        out.push({ ...(MAIN_INFO[a.id] ?? { label: a.label, hint: "" }), action: a.id });
      }
    }
    return out;
  }
  return actions.map((a) => {
    if (a.id === ACTION.CHALLENGE) return { label: "Challenge!", hint: "call the bluff", action: a.id, tone: "danger" };
    if (a.id === ACTION.PASS) return { label: phase === "block" ? "Allow" : "Pass", hint: phase === "block" ? "don't block" : "let it happen", action: a.id, tone: "muted" };
    if (BLOCK_ROLE[a.id]) return { label: `Block`, hint: `claim ${BLOCK_ROLE[a.id]}`, action: a.id, tone: "block", color: ROLE_COLORS[BLOCK_ROLE[a.id]] };
    if (a.id >= ACTION.DISCARD0 && a.id <= ACTION.DISCARD0 + 1) {
      const c = cards[a.id - ACTION.DISCARD0];
      const name = c ? ROLE_NAMES[c.type] : "card";
      return { label: `Lose ${name}`, hint: "reveal this card", action: a.id, color: ROLE_COLORS[name as string] };
    }
    return { label: a.label, action: a.id };
  });
}

export default function ActionPicker({ actions, onAction, players, phase, cards, coins, disabled, onTargetHover }: ActionPickerProps) {
  const [selected, setSelected] = useState(0);
  const [category, setCategory] = useState<Category | null>(null);
  const options = useMemo(() => buildOptions(actions, phase, category, players, cards), [actions, phase, category, players, cards]);

  // New decision: reset the menu. Default cursor to Pass in response windows (the safe choice).
  const actionKey = actions.map((a) => a.id).join(",") + phase;
  useLayoutEffect(() => {
    setCategory(null);
    const passIdx = actions.findIndex((a) => a.id === ACTION.PASS);
    setSelected(phase !== "action" && passIdx >= 0 ? passIdx : 0);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [actionKey]);

  // Must-coup: jump straight to target selection.
  useEffect(() => {
    if (phase === "action" && actions.length > 0 && actions.every((a) => CATEGORY_OF(a.id) === "coup")) setCategory("coup");
  }, [actionKey, phase, actions]);

  useEffect(() => {
    if (!onTargetHover) return;
    const opt = options[selected];
    onTargetHover(category && opt && typeof opt.action === "number" ? actionTarget(opt.action) : null);
  }, [category, selected, options, onTargetHover]);
  useEffect(() => () => onTargetHover?.(null), [onTargetHover]);

  const handleSelect = useCallback((opt: MenuOption) => {
    if (disabled) return;
    if (typeof opt.action === "string") {
      setCategory(opt.action);
      setSelected(0);
    } else {
      onAction(opt.action);
    }
  }, [onAction, disabled]);

  const cols = options.length <= 2 ? options.length : options.length === 4 ? 2 : 3;
  const move = (dx: number, dy: number) => setSelected((s) => {
    const n = options.length;
    if (dy) return Math.min(n - 1, Math.max(0, s + dy * cols));
    return (s + dx + n) % n;
  });
  const byId = (id: number) => options.find((o) => o.action === id);
  const blocks = options.filter((o) => typeof o.action === "number" && BLOCK_ROLE[o.action]);

  useKeyboard(
    {
      ArrowLeft: () => move(-1, 0),
      ArrowRight: () => move(1, 0),
      ArrowUp: () => move(0, -1),
      ArrowDown: () => move(0, 1),
      Enter: () => options[selected] && handleSelect(options[selected]),
      Escape: () => { if (category && !actions.every((a) => CATEGORY_OF(a.id) === "coup")) { setCategory(null); setSelected(0); } },
      Backspace: () => { if (category) { setCategory(null); setSelected(0); } },
      c: () => { const o = byId(ACTION.CHALLENGE); if (o) handleSelect(o); },
      p: () => { const o = byId(ACTION.PASS); if (o) handleSelect(o); },
      b: () => { if (blocks.length === 1) handleSelect(blocks[0]); },
      ...Object.fromEntries(options.slice(0, 9).map((o, i) => [String(i + 1), () => handleSelect(o)])),
    },
    [options, selected, category, handleSelect, disabled],
    options.length > 0,
  );

  if (options.length === 0) return null;

  return (
    <div className="border-t border-border-term pt-2">
      {category && (
        <div className="flex items-center justify-between text-sm mb-1">
          <span className="text-text-bright">{CATEGORY_INFO[category].label} → choose a target</span>
          {!actions.every((a) => CATEGORY_OF(a.id) === "coup") && (
            <button onClick={() => { setCategory(null); setSelected(0); }} className="text-text-dim hover:text-text-default text-xs border border-border-term px-2 py-0.5">
              ← back <span className="hidden sm:inline">(Esc)</span>
            </button>
          )}
        </div>
      )}
      {phase === "action" && !category && coins >= 7 && coins < 10 && (
        <div className="text-xs text-text-dim mb-1">You can afford a Coup.</div>
      )}
      <div className="grid gap-1" style={{ gridTemplateColumns: `repeat(${cols}, minmax(0, 1fr))` }} role="group" aria-label="Your options">
        {options.map((opt, i) => {
          const isSel = i === selected;
          const tone =
            opt.tone === "danger" ? "border-dead text-dead" :
            opt.tone === "muted" ? "border-border-term text-text-default" :
            "border-border-term text-text-default";
          return (
            <button
              key={String(opt.action)}
              onClick={() => handleSelect(opt)}
              onMouseEnter={() => setSelected(i)}
              disabled={disabled}
              className={`text-left px-2 py-2 sm:py-1.5 border transition-colors disabled:opacity-50 disabled:cursor-wait ${tone} ${isSel ? "bg-selection-bg/50" : "hover:bg-selection-bg/30"}`}
              style={opt.color ? { borderColor: opt.color } : undefined}
            >
              <div className="flex items-baseline gap-1 text-sm">
                <span className="text-text-dim text-xs hidden sm:inline">{i + 1}</span>
                <span className="font-bold" style={opt.color ? { color: opt.color } : undefined}>{opt.label}</span>
              </div>
              {opt.hint && <div className="text-[0.7rem] text-text-dim leading-tight">{opt.hint}</div>}
            </button>
          );
        })}
      </div>
      <div className="text-center text-text-dim text-[0.7rem] mt-1 hidden sm:block">
        1–9 / arrows + Enter{phase !== "action" && phase !== "lose_card" ? " · C challenge · P pass" : ""} · H history · ? rules
      </div>
    </div>
  );
}
