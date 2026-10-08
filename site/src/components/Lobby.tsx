"use client";

import { useState, useCallback } from "react";
import type { BotDifficulty, ClientMessage, LobbyView } from "@/lib/types";
import { useKeyboard } from "@/hooks/useKeyboard";

const DIFFS: BotDifficulty[] = ["easy", "medium", "hard"];
const nextDiff = (d: BotDifficulty) => DIFFS[(DIFFS.indexOf(d) + 1) % DIFFS.length];

interface LobbyProps {
  lobby: LobbyView;
  send: (msg: ClientMessage) => void;
  onLeave: () => void;
}

export default function Lobby({ lobby, send, onLeave }: LobbyProps) {
  const { code, seats, rules, isHost, maxSeats } = lobby;
  const [copied, setCopied] = useState<"link" | "code" | null>(null);
  const [botDiff, setBotDiff] = useState<BotDifficulty>("medium");
  const host = seats.find((s) => s.host);
  const full = seats.length >= maxSeats;
  const canStart = isHost && seats.length >= 2;

  const link = typeof window !== "undefined" ? `${window.location.origin}/lobby/${code}` : `/lobby/${code}`;

  const copy = useCallback(async (what: "link" | "code") => {
    const text = what === "link" ? link : code;
    try {
      if (what === "link" && navigator.share && window.matchMedia("(pointer: coarse)").matches) {
        await navigator.share({ title: "Join my Coup game", text: `Join my Coup game — room ${code}`, url: link });
        return;
      }
      await navigator.clipboard.writeText(text);
      setCopied(what);
      setTimeout(() => setCopied(null), 1800);
    } catch {
      // Share sheet dismissed or clipboard blocked; fall back to a prompt the user can copy from.
      if (what === "link") window.prompt("Copy this invite link:", text);
    }
  }, [link, code]);

  useKeyboard(
    {
      Enter: () => { if (canStart) send({ type: "start" }); },
      b: () => { if (isHost && !full) send({ type: "add_bot", difficulty: botDiff }); },
      Escape: onLeave,
    },
    [canStart, isHost, full, botDiff, onLeave, send],
  );

  return (
    <main className="min-h-dvh flex flex-col items-center justify-center p-4 gap-4">
      <div className="text-center">
        <h1 className="text-you text-xl mb-2">LOBBY</h1>
        <div className="flex items-center gap-2 justify-center flex-wrap">
          <span className="text-text-dim">room</span>
          <button
            onClick={() => copy("code")}
            className="text-cursor text-3xl tracking-[0.3em] font-bold hover:opacity-80"
            title="Copy code"
            aria-label={`Room code ${code.split("").join(" ")}, click to copy`}
          >
            {code}
          </button>
        </div>
        <div className="flex gap-2 justify-center mt-2">
          <button onClick={() => copy("link")} className="border border-you text-you px-3 py-1 text-sm hover:bg-you/10">
            {copied === "link" ? "✓ link copied" : "invite link"}
          </button>
          <button onClick={() => copy("code")} className="border border-border-term text-text-dim px-3 py-1 text-sm hover:text-text-default">
            {copied === "code" ? "✓ copied" : "copy code"}
          </button>
        </div>
        <p className="text-text-dim text-xs mt-2">Friends can open the link, or enter the code under “Join Game”.</p>
      </div>

      <section className="w-full max-w-md border border-border-term" aria-label="Players">
        <div className="border-b border-border-term px-3 py-1 text-text-dim text-xs flex justify-between">
          <span>{"// players"}</span>
          <span>{seats.length}/{maxSeats}</span>
        </div>
        <ul className="divide-y divide-border-term">
          {seats.map((s, i) => (
            <li key={`${s.name}-${i}`} className="flex items-center justify-between px-3 py-2 text-sm gap-2">
              <span className="flex items-center gap-2 min-w-0">
                <span className={`w-2 h-2 rounded-full shrink-0 ${s.online ? "bg-coin-gain" : "bg-text-dim"}`} title={s.online ? "online" : "offline"} />
                <span className={`truncate ${s.you ? "text-you" : "text-text-default"}`}>{s.name}</span>
                {s.you && <span className="text-xs text-you">(you)</span>}
                {s.host && <span className="text-xs text-cursor" title="Host">★ host</span>}
                {!s.online && !s.bot && <span className="text-xs text-text-dim">offline</span>}
              </span>
              <span className="flex items-center gap-2 shrink-0">
                {s.bot && (isHost ? (
                  <button
                    onClick={() => send({ type: "set_bot", index: i, difficulty: nextDiff(s.bot!) })}
                    className="text-xs border border-border-term px-2 py-0.5 text-text-dim hover:text-text-default"
                    title="Change difficulty"
                  >
                    bot · {s.bot}
                  </button>
                ) : (
                  <span className="text-xs text-text-dim">bot · {s.bot}</span>
                ))}
                {isHost && !s.host && (
                  <button
                    onClick={() => send({ type: "remove_seat", index: i })}
                    className="text-text-dim hover:text-dead text-xs px-1"
                    aria-label={`Remove ${s.name}`}
                    title="Remove"
                  >
                    ✕
                  </button>
                )}
              </span>
            </li>
          ))}
          {Array.from({ length: Math.max(0, maxSeats - seats.length) }).map((_, i) => (
            <li key={`open-${i}`} className="px-3 py-2 text-sm text-text-dim opacity-60">open seat</li>
          ))}
        </ul>
        {isHost && !full && (
          <div className="border-t border-border-term px-3 py-2 flex items-center justify-between gap-2 flex-wrap">
            <div className="flex gap-1" role="radiogroup" aria-label="New bot difficulty">
              {DIFFS.map((d) => (
                <button
                  key={d}
                  role="radio"
                  aria-checked={botDiff === d}
                  onClick={() => setBotDiff(d)}
                  className={`px-2 py-0.5 text-xs border ${botDiff === d ? "border-you text-you" : "border-border-term text-text-dim hover:text-text-default"}`}
                >
                  {d}
                </button>
              ))}
            </div>
            <button
              onClick={() => send({ type: "add_bot", difficulty: botDiff })}
              className="text-sm border border-border-term px-3 py-1 hover:border-cursor"
            >
              + add bot <span className="text-xs text-text-dim hidden sm:inline">(B)</span>
            </button>
          </div>
        )}
      </section>

      <section className="w-full max-w-md border border-border-term" aria-label="House rules">
        <div className="border-b border-border-term px-3 py-1 text-text-dim text-xs">{"// house rules"}</div>
        <Toggle
          label="Refund assassin on caught bluff"
          desc="If an Assassin claim is successfully challenged, the 3 coins come back (official rule)."
          on={rules.refundOnChallenge}
          editable={isHost}
          onToggle={() => send({ type: "rules", rules: { refundOnChallenge: !rules.refundOnChallenge } })}
        />
        <Toggle
          label="20s timer on challenges & blocks"
          desc="Undecided players auto-pass so nobody stalls the table."
          on={rules.responseTimer}
          editable={isHost}
          onToggle={() => send({ type: "rules", rules: { responseTimer: !rules.responseTimer } })}
        />
      </section>

      <div className="w-full max-w-md space-y-2">
        {isHost ? (
          <>
            <button
              onClick={() => send({ type: "start" })}
              disabled={!canStart}
              className="w-full border border-you text-you py-2 hover:bg-you/10 transition-colors disabled:opacity-30 disabled:cursor-not-allowed"
            >
              {">> START GAME <<"} <span className="text-xs opacity-60 hidden sm:inline">(Enter)</span>
            </button>
            {!canStart && <p className="text-text-dim text-xs text-center">Add a bot or invite a friend — Coup needs 2–6 players.</p>}
          </>
        ) : (
          <p className="text-text-dim text-sm text-center animate-pulse">Waiting for {host?.name ?? "the host"} to start…</p>
        )}
        <button onClick={onLeave} className="w-full text-text-dim hover:text-text-default text-sm py-1">
          {"< leave room"} <span className="text-xs hidden sm:inline">(Esc)</span>
        </button>
      </div>
    </main>
  );
}

function Toggle({ label, desc, on, editable, onToggle }: { label: string; desc: string; on: boolean; editable: boolean; onToggle: () => void }) {
  return (
    <div className="px-3 py-2">
      <div className="flex items-center justify-between text-sm gap-2">
        <span className="text-text-default">{label}</span>
        {editable ? (
          <button
            role="switch"
            aria-checked={on}
            aria-label={label}
            onClick={onToggle}
            className={`px-2 py-0.5 text-xs border transition-colors ${on ? "border-you text-you" : "border-border-term text-text-dim"}`}
          >
            {on ? "ON" : "OFF"}
          </button>
        ) : (
          <span className={`text-xs ${on ? "text-you" : "text-text-dim"}`}>{on ? "ON" : "OFF"}</span>
        )}
      </div>
      <p className="text-text-dim text-xs mt-0.5">{desc}</p>
    </div>
  );
}
