"use client";

import { useEffect, useState } from "react";

interface PhaseIndicatorProps {
  prompt: string;
  isYourTurn: boolean;
  deadline: number | null;
  deck: number;
  turn: number;
}

/** Live countdown (re-renders 4x/sec only while a deadline is active). */
function useCountdown(deadline: number | null): number | null {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (deadline == null) return;
    setNow(Date.now());
    const id = setInterval(() => setNow(Date.now()), 250);
    return () => clearInterval(id);
  }, [deadline]);
  return deadline == null ? null : Math.max(0, deadline - now);
}

export default function PhaseIndicator({ prompt, isYourTurn, deadline, deck, turn }: PhaseIndicatorProps) {
  const left = useCountdown(deadline);
  const [total, setTotal] = useState<number | null>(null);
  useEffect(() => {
    setTotal(deadline == null ? null : Math.max(1, deadline - Date.now()));
  }, [deadline]);
  const secs = left == null ? null : Math.ceil(left / 1000);
  const urgent = secs != null && secs <= 5;

  return (
    <div
      className={`relative border px-2 sm:px-4 py-1.5 sm:py-2 text-center overflow-hidden ${isYourTurn ? "border-you" : "border-border-term"}`}
      role="status"
      aria-live="polite"
    >
      <div className="flex items-center justify-between text-[0.7rem] sm:text-xs text-text-dim mb-0.5">
        <span>turn {turn}</span>
        <span className={isYourTurn ? "text-you font-bold" : ""}>{isYourTurn ? "YOUR MOVE" : ""}</span>
        <span>deck {deck}</span>
      </div>
      <div className={`text-xs sm:text-sm ${isYourTurn ? "text-text-bright font-bold" : "text-text-default"}`}>
        {prompt}
        {secs != null && (
          <span className={`ml-2 tabular-nums ${urgent ? "text-coin-loss" : "text-text-dim"}`}>{secs}s</span>
        )}
      </div>
      {left != null && total != null && (
        <div
          aria-hidden
          className={`absolute left-0 bottom-0 h-0.5 ${urgent ? "bg-coin-loss" : "bg-cursor"}`}
          style={{ width: `${Math.min(100, (left / total) * 100)}%`, transition: "width 250ms linear" }}
        />
      )}
    </div>
  );
}
