// Client mirror of the server protocol (workers/src/types.ts). Keep in sync.

/** Must equal PROTOCOL in workers/src/types.ts. */
export const PROTOCOL = 2;

export type BotDifficulty = "easy" | "medium" | "hard";

export interface HouseRules {
  refundOnChallenge: boolean;
  responseTimer: boolean;
}

export type HistoryKind =
  | "turn" | "action" | "claim" | "challenge" | "block" | "reveal" | "lose" | "elim" | "info" | "win";

export interface HistoryEntry {
  turn: number;
  text: string;
  kind: HistoryKind;
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
  finalCards: number[][];
}

export interface CardInfo {
  type: number; // 0=Duke,1=Assassin,2=Captain,3=Ambassador,4=Contessa
  alive: boolean;
}

export interface PlayerInfo {
  name: string;
  coins: number;
  influence: number;
  revealed: number[];
  bot: boolean;
  online: boolean;
  away: boolean;
  alive: boolean;
  claims: string[];
}

export interface ActionInfo {
  id: number;
  label: string;
}

export interface GameView {
  type: "state";
  code: string;
  you: number;
  cards: CardInfo[];
  drawn?: number[];
  players: PlayerInfo[];
  phase: string;
  active: number;
  turnPlayer: number;
  pending?: number;
  block?: { seat: number; card: number };
  actions: ActionInfo[];
  prompt: string;
  deadlineMs?: number;
  history: HistoryEntry[];
  historyBase: number;
  deck: number;
  turn: number;
  step: number;
  isHost: boolean;
  rules: HouseRules;
}

export interface LobbyView {
  type: "lobby";
  code: string;
  seats: LobbySeat[];
  rules: HouseRules;
  isHost: boolean;
  maxSeats: number;
}

export type ServerMessage =
  | { type: "welcome"; protocol: number; code: string }
  | LobbyView
  | GameView
  | ({ type: "game_over" } & GameResult)
  | { type: "error"; message: string; fatal?: boolean }
  | { type: "pong" };

export type ClientMessage =
  | { type: "join"; name: string; cid: string }
  | { type: "leave" }
  | { type: "start" }
  | { type: "add_bot"; difficulty: BotDifficulty }
  | { type: "remove_seat"; index: number }
  | { type: "set_bot"; index: number; difficulty: BotDifficulty }
  | { type: "rules"; rules: Partial<HouseRules> }
  | { type: "action"; action: number; then?: number; step?: number }
  | { type: "ping" };

/** Game view as held by the client: history merged across messages, deadline made absolute. */
export interface GameState extends Omit<GameView, "history" | "historyBase" | "deadlineMs"> {
  history: HistoryEntry[];
  deadline: number | null;
}
