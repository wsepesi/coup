// Shared types and constants for the Coup Workers backend.
// Keep in sync with site/src/lib/types.ts (protocol) and c_engine/coup_core.h (indices).

/** Bump when the client<->server message format changes incompatibly. */
export const PROTOCOL = 2;

// ---- Card types ----
export const DUKE = 0;
export const ASSASSIN = 1;
export const CAPTAIN = 2;
export const AMBASSADOR = 3;
export const CONTESSA = 4;

export const CARD_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa"] as const;

// ---- Phases ----
export const PHASE_DEAL = 0;
export const PHASE_CHANCE_REDRAW = 1;
export const PHASE_CHANCE_EXCHANGE = 2;
export const PHASE_MAIN_ACTION = 3;
export const PHASE_CHALLENGE_ACTION = 4;
export const PHASE_BLOCK = 5;
export const PHASE_CHALLENGE_BLOCK = 6;
export const PHASE_LOSE_CARD = 7;
export const PHASE_EXCHANGE_DISCARD = 8;
export const PHASE_RESOLVE = 9;

export const PHASE_NAMES: Record<number, string> = {
  [PHASE_DEAL]: "deal",
  [PHASE_CHANCE_REDRAW]: "chance_redraw",
  [PHASE_CHANCE_EXCHANGE]: "chance_exchange",
  [PHASE_MAIN_ACTION]: "action",
  [PHASE_CHALLENGE_ACTION]: "challenge_action",
  [PHASE_BLOCK]: "block",
  [PHASE_CHALLENGE_BLOCK]: "challenge_block",
  [PHASE_LOSE_CARD]: "lose_card",
  [PHASE_EXCHANGE_DISCARD]: "exchange_discard",
  [PHASE_RESOLVE]: "resolve",
};

// ---- Actions (32 total) ----
export const ACTION_INCOME = 0;
export const ACTION_FOREIGN_AID = 1;
export const ACTION_TAX = 2;
export const ACTION_EXCHANGE = 3;
export const ACTION_COUP_P0 = 4;
export const ACTION_STEAL_P0 = 10;
export const ACTION_ASSASSINATE_P0 = 16;
export const ACTION_CHALLENGE = 22;
export const ACTION_PASS = 23;
export const ACTION_BLOCK_CONTESSA = 24;
export const ACTION_BLOCK_CAPTAIN = 25;
export const ACTION_BLOCK_AMBASSADOR = 26;
export const ACTION_BLOCK_DUKE = 27;
export const ACTION_DISCARD_SLOT0 = 28;
export const ACTION_DISCARD_SLOT1 = 29;
export const ACTION_DISCARD_SLOT2 = 30;
export const ACTION_DISCARD_SLOT3 = 31;

export const NUM_ACTIONS = 32;

export const isCoup = (a: number) => a >= ACTION_COUP_P0 && a < ACTION_COUP_P0 + 6;
export const isSteal = (a: number) => a >= ACTION_STEAL_P0 && a < ACTION_STEAL_P0 + 6;
export const isAssassinate = (a: number) => a >= ACTION_ASSASSINATE_P0 && a < ACTION_ASSASSINATE_P0 + 6;
export const isBlock = (a: number) => a >= ACTION_BLOCK_CONTESSA && a <= ACTION_BLOCK_DUKE;
export const isDiscard = (a: number) => a >= ACTION_DISCARD_SLOT0 && a <= ACTION_DISCARD_SLOT3;

export function actionTarget(action: number): number | null {
  if (isCoup(action)) return action - ACTION_COUP_P0;
  if (isSteal(action)) return action - ACTION_STEAL_P0;
  if (isAssassinate(action)) return action - ACTION_ASSASSINATE_P0;
  return null;
}

/** Role claimed by a main action, or null for income/foreign aid/coup. */
export function claimedRole(action: number): number | null {
  if (action === ACTION_TAX) return DUKE;
  if (action === ACTION_EXCHANGE) return AMBASSADOR;
  if (isSteal(action)) return CAPTAIN;
  if (isAssassinate(action)) return ASSASSIN;
  return null;
}

/** Role claimed by a block action. */
export function blockRole(action: number): number | null {
  if (action === ACTION_BLOCK_CONTESSA) return CONTESSA;
  if (action === ACTION_BLOCK_CAPTAIN) return CAPTAIN;
  if (action === ACTION_BLOCK_AMBASSADOR) return AMBASSADOR;
  if (action === ACTION_BLOCK_DUKE) return DUKE;
  return null;
}

export function actionLabel(action: number, names: string[], you: number): string {
  const who = (s: number) => (s === you ? "yourself" : names[s] ?? `Player ${s + 1}`);
  if (action === ACTION_INCOME) return "Income";
  if (action === ACTION_FOREIGN_AID) return "Foreign Aid";
  if (action === ACTION_TAX) return "Tax";
  if (action === ACTION_EXCHANGE) return "Exchange";
  if (isCoup(action)) return `Coup ${who(action - ACTION_COUP_P0)}`;
  if (isSteal(action)) return `Steal from ${who(action - ACTION_STEAL_P0)}`;
  if (isAssassinate(action)) return `Assassinate ${who(action - ACTION_ASSASSINATE_P0)}`;
  if (action === ACTION_CHALLENGE) return "Challenge";
  if (action === ACTION_PASS) return "Pass";
  const br = blockRole(action);
  if (br != null) return `Block (${CARD_NAMES[br]})`;
  if (isDiscard(action)) return `Discard card ${action - ACTION_DISCARD_SLOT0 + 1}`;
  return `Action ${action}`;
}

// ---- Room / protocol types ----

export type BotDifficulty = "easy" | "medium" | "hard";
export const isDifficulty = (d: unknown): d is BotDifficulty => d === "easy" || d === "medium" || d === "hard";

export interface HouseRules {
  /** Return the 3 coins of an assassination whose claim is successfully challenged (official rule). */
  refundOnChallenge: boolean;
  /** Auto-pass a connected human's challenge/block decision after RESPONSE_TIMEOUT_MS. */
  responseTimer: boolean;
}

export const DEFAULT_RULES: HouseRules = { refundOnChallenge: true, responseTimer: true };

export type HistoryKind =
  | "turn" | "action" | "claim" | "challenge" | "block" | "pass" | "reveal" | "lose" | "elim" | "info" | "win";

export interface HistoryEntry {
  turn: number;
  text: string;
  kind: HistoryKind;
  /** Seat the entry is about (actor, claimant, or the player losing a card). Absent for table-wide notes. */
  seat?: number;
}

export interface LobbySeat {
  name: string;
  bot: BotDifficulty | null;
  online: boolean;
  host: boolean;
  you: boolean;
}

export interface Standing {
  seat: number;
  name: string;
  bot: boolean;
  alive: boolean;
  eliminatedTurn?: number;
}

export interface GameResult {
  winner: number;
  winnerName: string;
  standings: Standing[];
  turns: number;
  history: HistoryEntry[];
  /** Hidden cards of survivors, revealed at the end. */
  finalCards: number[][];
}

export interface PlayerView {
  name: string;
  coins: number;
  influence: number;
  revealed: number[];
  bot: boolean;
  online: boolean;
  /** Human seat currently auto-played (disconnected past grace, or left). */
  away: boolean;
  alive: boolean;
  claims: string[];
}

export interface GameView {
  type: "state";
  code: string;
  you: number; // -1 = spectator
  cards: { type: number; alive: boolean }[];
  /** Two drawn cards, only for the exchanging player during exchange_discard. */
  drawn?: number[];
  players: PlayerView[];
  phase: string;
  active: number;
  turnPlayer: number;
  /** Main action under resolution (absent during the action phase). */
  pending?: number;
  /** Current block, during challenge_block. */
  block?: { seat: number; card: number };
  actions: { id: number; label: string }[];
  prompt: string;
  /** Milliseconds until the server auto-acts for the active player. */
  deadlineMs?: number;
  /** History entries starting at absolute index historyBase. */
  history: HistoryEntry[];
  historyBase: number;
  deck: number;
  turn: number;
  /** Decision counter; echo it with actions so stale clicks are ignored. */
  step: number;
  isHost: boolean;
  rules: HouseRules;
}

export type ServerMessage =
  | { type: "welcome"; protocol: number; code: string }
  | {
      type: "lobby";
      code: string;
      seats: LobbySeat[];
      rules: HouseRules;
      isHost: boolean;
      maxSeats: number;
      lastResult?: GameResult;
    }
  | GameView
  | ({ type: "game_over" } & GameResult)
  | { type: "error"; message: string; fatal?: boolean }
  | { type: "pong" };

// ---- Env type ----
export interface Env {
  GAME: DurableObjectNamespace;
}
