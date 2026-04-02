// T3: Shared types — enums and state types matching C engine

export enum CardType {
  Duke = 0,
  Assassin = 1,
  Captain = 2,
  Ambassador = 3,
  Contessa = 4,
}

export enum Phase {
  Deal = 0,
  ChanceRedraw = 1,
  ChanceExchange = 2,
  MainAction = 3,
  ChallengeAction = 4,
  Block = 5,
  ChallengeBlock = 6,
  LoseCard = 7,
  ExchangeDiscard = 8,
  Resolve = 9,
}

// Flat action space 0-31 matching plan.md §4
export enum Action {
  Income = 0,
  ForeignAid = 1,
  Tax = 2,
  Exchange = 3,
  // Coup targets: 4-9 (player 0-5)
  CoupP0 = 4, CoupP1 = 5, CoupP2 = 6, CoupP3 = 7, CoupP4 = 8, CoupP5 = 9,
  // Steal targets: 10-15 (player 0-5)
  StealP0 = 10, StealP1 = 11, StealP2 = 12, StealP3 = 13, StealP4 = 14, StealP5 = 15,
  // Assassinate targets: 16-21 (player 0-5)
  AssassinateP0 = 16, AssassinateP1 = 17, AssassinateP2 = 18,
  AssassinateP3 = 19, AssassinateP4 = 20, AssassinateP5 = 21,
  Challenge = 22,
  Pass = 23,
  BlockContessa = 24,
  BlockCaptain = 25,
  BlockAmbassador = 26,
  BlockDuke = 27,
  DiscardSlot0 = 28,
  DiscardSlot1 = 29,
  DiscardSlot2 = 30,
  DiscardSlot3 = 31,
}

export const NUM_ACTIONS = 32;
export const OBS_SIZE = 407;

export interface CardState {
  type: CardType;
  alive: boolean;
}

export interface PlayerState {
  coins: number;
  influence: number;
  cards: [CardState, CardState];
  alive: boolean;
}

export interface GameSnapshot {
  phase: Phase;
  activePlayer: number;
  turnPlayer: number;
  pendingAction: number;
  players: PlayerState[];
  numPlayers: number;
  done: boolean;
  winner: number;
  validMask: number;
  exchangeCards?: [CardType, CardType];
}

// Action category helpers
export function actionCategory(action: number): string {
  if (action === Action.Income) return "Income";
  if (action === Action.ForeignAid) return "Foreign Aid";
  if (action === Action.Tax) return "Tax";
  if (action === Action.Exchange) return "Exchange";
  if (action >= 4 && action <= 9) return "Coup";
  if (action >= 10 && action <= 15) return "Steal";
  if (action >= 16 && action <= 21) return "Assassinate";
  if (action === Action.Challenge) return "Challenge";
  if (action === Action.Pass) return "Pass";
  if (action >= 24 && action <= 27) return "Block";
  if (action >= 28 && action <= 31) return "Discard";
  return "Unknown";
}

export function actionTarget(action: number): number | null {
  if (action >= 4 && action <= 9) return action - 4;
  if (action >= 10 && action <= 15) return action - 10;
  if (action >= 16 && action <= 21) return action - 16;
  return null;
}

export function blockCardType(action: number): CardType | null {
  switch (action) {
    case Action.BlockContessa: return CardType.Contessa;
    case Action.BlockCaptain: return CardType.Captain;
    case Action.BlockAmbassador: return CardType.Ambassador;
    case Action.BlockDuke: return CardType.Duke;
    default: return null;
  }
}

export function claimedRole(action: number): CardType | null {
  if (action === Action.Tax) return CardType.Duke;
  if (action === Action.Exchange) return CardType.Ambassador;
  if (action >= 10 && action <= 15) return CardType.Captain;
  if (action >= 16 && action <= 21) return CardType.Assassin;
  return null;
}

export const CARD_NAMES: Record<CardType, string> = {
  [CardType.Duke]: "Duke",
  [CardType.Assassin]: "Assassin",
  [CardType.Captain]: "Captain",
  [CardType.Ambassador]: "Ambassador",
  [CardType.Contessa]: "Contessa",
};
