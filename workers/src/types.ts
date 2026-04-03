// Shared types and constants for the Coup Workers backend

// ---- Card types ----
export const DUKE = 0;
export const ASSASSIN = 1;
export const CAPTAIN = 2;
export const AMBASSADOR = 3;
export const CONTESSA = 4;

export const CARD_NAMES: Record<number, string> = {
  [DUKE]: "Duke",
  [ASSASSIN]: "Assassin",
  [CAPTAIN]: "Captain",
  [AMBASSADOR]: "Ambassador",
  [CONTESSA]: "Contessa",
};

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
export const OBS_SIZE = 407;

// ---- Helper functions ----

export function getValidActionsFromMask(mask: number): number[] {
  const actions: number[] = [];
  for (let i = 0; i < 32; i++) {
    if ((mask >>> i) & 1) actions.push(i);
  }
  return actions;
}

export function actionTarget(action: number): number | null {
  if (action >= 4 && action <= 9) return action - 4;
  if (action >= 10 && action <= 15) return action - 10;
  if (action >= 16 && action <= 21) return action - 16;
  return null;
}

export function claimedRole(action: number): number | null {
  if (action === ACTION_TAX) return DUKE;
  if (action === ACTION_EXCHANGE) return AMBASSADOR;
  if (action >= ACTION_STEAL_P0 && action <= ACTION_STEAL_P0 + 5) return CAPTAIN;
  if (action >= ACTION_ASSASSINATE_P0 && action <= ACTION_ASSASSINATE_P0 + 5) return ASSASSIN;
  return null;
}

export function actionLabel(action: number, playerNames: string[]): string {
  if (action === ACTION_INCOME) return "Income";
  if (action === ACTION_FOREIGN_AID) return "Foreign Aid";
  if (action === ACTION_TAX) return "Tax (Duke)";
  if (action === ACTION_EXCHANGE) return "Exchange (Ambassador)";

  if (action >= 4 && action <= 9) {
    const t = action - 4;
    return `Coup \u2192 ${playerNames[t] ?? `Player ${t}`}`;
  }
  if (action >= 10 && action <= 15) {
    const t = action - 10;
    return `Steal from ${playerNames[t] ?? `Player ${t}`} (Captain)`;
  }
  if (action >= 16 && action <= 21) {
    const t = action - 16;
    return `Assassinate ${playerNames[t] ?? `Player ${t}`} (Assassin)`;
  }

  if (action === ACTION_CHALLENGE) return "Challenge";
  if (action === ACTION_PASS) return "Pass";
  if (action === ACTION_BLOCK_CONTESSA) return "Block (Contessa)";
  if (action === ACTION_BLOCK_CAPTAIN) return "Block (Captain)";
  if (action === ACTION_BLOCK_AMBASSADOR) return "Block (Ambassador)";
  if (action === ACTION_BLOCK_DUKE) return "Block (Duke)";

  if (action === ACTION_DISCARD_SLOT0) return "Discard Card 1";
  if (action === ACTION_DISCARD_SLOT1) return "Discard Card 2";
  if (action === ACTION_DISCARD_SLOT2) return "Discard Card 3";
  if (action === ACTION_DISCARD_SLOT3) return "Discard Card 4";

  return `Action ${action}`;
}

// ---- Protocol types ----

export type BotDifficulty = "easy" | "medium" | "hard";

export interface HouseRules {
  refundOnChallenge?: boolean;
}

export interface PlayerSlot {
  username: string;
  seat: number;
  isBot: boolean;
  botDifficulty?: BotDifficulty;
  ready: boolean;
}

// Client -> Server messages
export type ClientMessage =
  | { type: "create"; username: string; numPlayers: number; numBots: number; botDifficulty: BotDifficulty; houseRules?: HouseRules }
  | { type: "join"; username: string; code: string }
  | { type: "quick_play"; username: string }
  | { type: "action"; action: number }
  | { type: "start" }
  | { type: "house_rules"; houseRules: HouseRules }
  | { type: "bot_config"; numBots: number; botDifficulty: BotDifficulty }
  | { type: "forfeit" };

// Server -> Client messages
export type ServerMessage =
  | { type: "room_created"; code: string }
  | { type: "lobby"; code: string; players: PlayerSlot[]; houseRules?: HouseRules; isHost?: boolean; numBots: number; botDifficulty: BotDifficulty; numPlayers: number }
  | {
      type: "state";
      yourSeat: number;
      yourCards: { type: number; alive: boolean }[];
      players: {
        name: string;
        coins: number;
        influence: number;
        revealed: { type: number }[];
        isBot: boolean;
        alive: boolean;
      }[];
      phase: string;
      activePlayer: number;
      isYourTurn: boolean;
      availableActions: { id: number; label: string }[];
      history: { text: string; turn: number }[];
      claims: Record<number, string[]>;
      context?: string;
      pendingAction?: number;
      deckSize: number;
    }
  | {
      type: "game_over";
      winner: number;
      winnerName: string;
      finalStandings: { seat: number; name: string; alive: boolean; eliminatedTurn?: number }[];
      totalTurns: number;
      history: { text: string; turn: number }[];
    }
  | { type: "error"; message: string }
  | { type: "forfeited"; by: string };

// ---- Env type ----
export interface Env {
  MATCHMAKER: DurableObjectNamespace;
  GAME: DurableObjectNamespace;
}
