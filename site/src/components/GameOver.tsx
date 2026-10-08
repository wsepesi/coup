"use client";

import { useState, useCallback } from "react";
import type { GameResult, LobbyView } from "@/lib/types";
import { ROLE_NAMES, ROLE_COLORS } from "@/lib/constants";
import { useKeyboard } from "@/hooks/useKeyboard";
import { HistoryOverlay } from "./HistoryLog";

interface GameOverProps {
  result: GameResult;
  lobby: LobbyView | null;
  youName: string | null;
  onRematch: () => void;
  onLobby: () => void;
  onHome: () => void;
}

const ORDINALS = ["1st", "2nd", "3rd", "4th", "5th", "6th"];

export default function GameOver({ result, lobby, youName, onRematch, onLobby, onHome }: GameOverProps) {
  const [showHistory, setShowHistory] = useState(false);
  const [copied, setCopied] = useState(false);
  const isHost = !!lobby?.isHost;
  const canRematch = isHost && (lobby?.seats.length ?? 0) >= 2;
  const youWon = youName != null && result.winnerName === youName;

  const copyHistory = useCallback(() => {
    const text = result.history.map((h) => `[T${h.turn}] ${h.text}`).join("\n");
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    }).catch(() => {});
  }, [result.history]);

  useKeyboard(
    {
      Enter: () => (canRematch ? onRematch() : onLobby()),
      r: () => { if (canRematch) onRematch(); },
      l: onLobby,
      m: onHome,
      h: () => setShowHistory((v) => !v),
      c: copyHistory,
    },
    [canRematch, onRematch, onLobby, onHome, copyHistory],
    !showHistory,
  );

  return (
    <main className="min-h-dvh flex items-center justify-center p-4">
      <div className="border border-border-term p-6 sm:p-8 max-w-md w-full text-center">
        <h1 className="text-you text-3xl mb-2">GAME OVER</h1>
        <div className="text-cursor text-2xl mb-1">{youWon ? "You win!" : `${result.winnerName} wins!`}</div>
        <div className="text-text-dim text-xs mb-5">{result.turns} turns</div>

        <ol className="border border-border-term mb-5 divide-y divide-border-term text-left">
          {result.standings.map((s, i) => {
            const cards = result.finalCards[s.seat] ?? [];
            return (
              <li key={s.seat} className="flex items-center gap-3 p-2 text-sm">
                <span className="text-text-dim w-8">{ORDINALS[i] ?? `${i + 1}th`}</span>
                <span className={`flex-1 truncate ${s.seat === result.winner ? "text-you" : "text-text-default"}`}>
                  {s.name}{s.bot && <span className="text-text-dim text-xs"> · bot</span>}
                </span>
                {s.alive ? (
                  <span className="text-xs">
                    {cards.map((t, k) => (
                      <span key={k} className="ml-1" style={{ color: ROLE_COLORS[ROLE_NAMES[t]] }}>{ROLE_NAMES[t]}</span>
                    ))}
                  </span>
                ) : (
                  <span className="text-text-dim text-xs">out T{s.eliminatedTurn}</span>
                )}
              </li>
            );
          })}
        </ol>

        <div className="space-y-2">
          {isHost ? (
            <button
              onClick={onRematch}
              disabled={!canRematch}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-30"
            >
              {">> PLAY AGAIN <<"} <span className="text-xs opacity-60 hidden sm:inline">(Enter)</span>
            </button>
          ) : (
            <p className="text-text-dim text-sm animate-pulse">Waiting for the host to start a rematch…</p>
          )}
          <div className="grid grid-cols-3 gap-2 text-sm">
            <button onClick={onLobby} className="border border-border-term text-text-dim py-2 hover:text-text-default">lobby <span className="text-xs opacity-60 hidden sm:inline">(L)</span></button>
            <button onClick={() => setShowHistory(true)} className="border border-border-term text-text-dim py-2 hover:text-text-default">history <span className="text-xs opacity-60 hidden sm:inline">(H)</span></button>
            <button onClick={onHome} className="border border-border-term text-text-dim py-2 hover:text-text-default">menu <span className="text-xs opacity-60 hidden sm:inline">(M)</span></button>
          </div>
        </div>
      </div>
      {showHistory && (
        <HistoryOverlay entries={result.history} onClose={() => setShowHistory(false)} onCopy={copyHistory} copied={copied} />
      )}
    </main>
  );
}
