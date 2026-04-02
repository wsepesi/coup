// T6: History renderer — pure functions: game event → human-readable string

import { Action, CardType, CARD_NAMES, actionTarget, blockCardType, claimedRole } from "./types.js";

export type GameEventType =
  | "income" | "foreign_aid" | "tax" | "steal" | "assassinate"
  | "coup" | "exchange" | "challenge" | "challenge_result"
  | "block" | "block_result" | "lose_card" | "elimination"
  | "pass" | "foreign_aid_blocked";

export interface GameEvent {
  type: GameEventType;
  player: number;
  playerName: string;
  isHuman?: boolean;
  target?: number;
  targetName?: string;
  targetIsHuman?: boolean;
  action?: number;
  card?: CardType;
  success?: boolean;
  blockCard?: CardType;
}

// Grammar helpers for "You" vs third-person
function verb(name: string, isHuman: boolean | undefined, v3rd: string, v2nd: string): string {
  return isHuman ? `${name} ${v2nd}` : `${name} ${v3rd}`;
}

function possessive(name: string, isHuman: boolean | undefined): string {
  return isHuman ? "Your" : `${name}'s`;
}

export function renderEvent(event: GameEvent): string {
  const p = event.playerName;
  const human = event.isHuman;
  const t = event.targetName ?? `Player ${event.target}`;
  const tHuman = event.targetIsHuman;

  switch (event.type) {
    case "income":
      return `${verb(p, human, "took", "take")} Income. +1 coin.`;
    case "foreign_aid":
      return `${verb(p, human, "took", "take")} Foreign Aid. +2 coins.`;
    case "foreign_aid_blocked":
      return `${possessive(p, human)} Foreign Aid was blocked by ${t} (Duke).`;
    case "tax":
      return `${verb(p, human, "claims", "claim")} Duke for Tax. +3 coins.`;
    case "steal":
      return `${verb(p, human, "claims", "claim")} Captain to Steal from ${t}.`;
    case "assassinate":
      return `${verb(p, human, "claims", "claim")} Assassin to Assassinate ${t}. -3 coins.`;
    case "coup":
      return `${verb(p, human, "Coups", "Coup")} ${t}. -7 coins.`;
    case "exchange":
      return `${verb(p, human, "claims", "claim")} Ambassador for Exchange.`;
    case "challenge": {
      const role = event.card != null ? CARD_NAMES[event.card] : "claim";
      return `${verb(p, human, "challenges", "challenge")} ${possessive(t, tHuman)} ${role}!`;
    }
    case "challenge_result": {
      if (event.success) {
        return `Challenge succeeded! ${t} was bluffing.`;
      } else {
        const role = event.card != null ? CARD_NAMES[event.card] : "card";
        return `Challenge failed! ${verb(t, tHuman, "reveals", "reveal")} ${role}.`;
      }
    }
    case "block": {
      const card = event.blockCard != null ? CARD_NAMES[event.blockCard] : "card";
      return `${verb(p, human, "blocks", "block")} with ${card}.`;
    }
    case "block_result": {
      if (event.success) {
        return `Block by ${p} was not challenged. Action blocked.`;
      } else {
        return `${possessive(p, human)} block was challenged!`;
      }
    }
    case "lose_card": {
      const card = event.card != null ? CARD_NAMES[event.card] : "a card";
      return `${verb(p, human, "loses", "lose")} ${card}.`;
    }
    case "elimination":
      return `${verb(p, human, "has been", "have been")} eliminated!`;
    case "pass":
      return `${verb(p, human, "passes", "pass")}.`;
    default:
      return `${p} does something.`;
  }
}

export function describeAction(action: number, playerName: string, isHuman?: boolean, targetName?: string): string {
  const t = targetName ?? "someone";
  if (action === Action.Income) return `${verb(playerName, isHuman, "takes", "take")} Income`;
  if (action === Action.ForeignAid) return `${verb(playerName, isHuman, "takes", "take")} Foreign Aid`;
  if (action === Action.Tax) return `${verb(playerName, isHuman, "claims", "claim")} Duke for Tax`;
  if (action === Action.Exchange) return `${verb(playerName, isHuman, "claims", "claim")} Ambassador for Exchange`;
  if (action >= 4 && action <= 9) return `${verb(playerName, isHuman, "Coups", "Coup")} ${t}`;
  if (action >= 10 && action <= 15) return `${verb(playerName, isHuman, "claims", "claim")} Captain to Steal from ${t}`;
  if (action >= 16 && action <= 21) return `${verb(playerName, isHuman, "claims", "claim")} Assassin to Assassinate ${t}`;
  if (action === Action.Challenge) return `${verb(playerName, isHuman, "challenges", "challenge")}`;
  if (action === Action.Pass) return `${verb(playerName, isHuman, "passes", "pass")}`;
  const bCard = blockCardType(action);
  if (bCard != null) return `${verb(playerName, isHuman, "blocks", "block")} with ${CARD_NAMES[bCard]}`;
  if (action >= 28 && action <= 31) return `${verb(playerName, isHuman, "discards", "discard")} a card`;
  return `${playerName} acts`;
}
