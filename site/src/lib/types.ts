// ── Server → Client messages ──────────────────────────────────────

export interface LobbyPlayer {
  seat: number;
  username: string;
  isBot: boolean;
  botDifficulty?: string;
}

export interface CardInfo {
  type: number; // 0=Duke,1=Assassin,2=Captain,3=Ambassador,4=Contessa
  alive: boolean;
}

export interface PlayerInfo {
  name: string;
  coins: number;
  influence: number; // 0, 1, or 2
  revealed: { type: number }[];
  isBot: boolean;
  alive: boolean;
}

export interface ActionInfo {
  id: number;
  label: string;
}

export interface HistoryEntry {
  text: string;
  turn: number;
}

export interface Standing {
  seat: number;
  name: string;
  alive: boolean;
}

export type ServerMessage =
  | { type: "room_created"; code: string }
  | { type: "lobby"; code: string; players: LobbyPlayer[] }
  | {
      type: "state";
      yourSeat: number;
      yourCards: CardInfo[];
      players: PlayerInfo[];
      phase: string;
      activePlayer: number;
      isYourTurn: boolean;
      availableActions: ActionInfo[];
      history: HistoryEntry[];
      context?: string;
      pendingAction?: number;
      deckSize: number;
    }
  | {
      type: "game_over";
      winner: number;
      winnerName: string;
      finalStandings: Standing[];
    }
  | { type: "error"; message: string };

// ── Client → Server messages ──────────────────────────────────────

export type ClientMessage =
  | {
      type: "create";
      username: string;
      numPlayers: number;
      numBots: number;
      botDifficulty: string;
    }
  | { type: "join"; username: string; code: string }
  | { type: "action"; action: number }
  | { type: "start" };

// ── Game state for React ──────────────────────────────────────────

export interface GameState {
  yourSeat: number;
  yourCards: CardInfo[];
  players: PlayerInfo[];
  phase: string;
  activePlayer: number;
  isYourTurn: boolean;
  availableActions: ActionInfo[];
  history: HistoryEntry[];
  context?: string;
  pendingAction?: number;
  deckSize: number;
}
