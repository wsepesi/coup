"use client";

import type { ActionInfo } from "@/lib/types";

interface ActionPickerProps {
  actions: ActionInfo[];
  onAction: (action: number) => void;
}

export default function ActionPicker({ actions, onAction }: ActionPickerProps) {
  if (actions.length === 0) return null;

  return (
    <div className="border-t border-border-term pt-3">
      <div className="text-text-dim text-xs mb-2">
        {"═".repeat(40)}
      </div>
      <div className="grid grid-cols-3 gap-1">
        {actions.map((a) => (
          <button
            key={a.id}
            onClick={() => onAction(a.id)}
            className="text-left text-sm px-2 py-1 hover:bg-selection-bg hover:text-text-bright transition-colors text-text-default"
          >
            <span className="text-text-dim mr-1">▸</span>
            {a.label}
          </button>
        ))}
      </div>
    </div>
  );
}
