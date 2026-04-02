// Heuristic bots for the Coup Workers backend.
// Easy = random legal action (always passes on challenges/blocks).
// Medium = rule-based honest play, never bluffs, blocks when holding the card.

import {
  ACTION_INCOME,
  ACTION_FOREIGN_AID,
  ACTION_TAX,
  ACTION_EXCHANGE,
  ACTION_CHALLENGE,
  ACTION_PASS,
  ACTION_BLOCK_CONTESSA,
  ACTION_BLOCK_CAPTAIN,
  ACTION_BLOCK_AMBASSADOR,
  ACTION_BLOCK_DUKE,
  ACTION_DISCARD_SLOT0,
  ACTION_DISCARD_SLOT3,
  ACTION_COUP_P0,
  ACTION_STEAL_P0,
  ACTION_ASSASSINATE_P0,
  DUKE,
  ASSASSIN,
  CAPTAIN,
  AMBASSADOR,
  CONTESSA,
  getValidActionsFromMask,
  type BotDifficulty,
} from "./types.js";
import type { CoupWasm } from "./wasm-bridge.js";

const BOT_NAMES = [
  "Matt", "Lucia", "Elisa", "Tyrone", "Abby", "Ren", "Sakura", "Pierre",
  "Haru", "Marco", "Emily", "Takumi", "Miyu", "Oscar", "Silke", "Theo",
  "Naomi", "Kenji", "Gabi", "Luca", "Yuki", "Steph", "Akira", "Dina",
  "Tommy", "Mia", "Fritz", "Elena", "Ravi", "Anna",
];

let namePool: string[] = [];

function shuffleNames(): void {
  namePool = [...BOT_NAMES];
  for (let i = namePool.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [namePool[i], namePool[j]] = [namePool[j], namePool[i]];
  }
}

export function pickBotName(): string {
  if (namePool.length === 0) shuffleNames();
  return namePool.pop()!;
}

export function chooseBotAction(
  difficulty: BotDifficulty,
  seat: number,
  wasm: CoupWasm,
): number {
  const mask = wasm.getValidActions();
  const actions = getValidActionsFromMask(mask);
  if (actions.length === 0) return ACTION_PASS;
  if (actions.length === 1) return actions[0];

  if (difficulty === "easy") {
    return easyAction(actions);
  }
  return mediumAction(actions, seat, wasm);
}

// ---- Easy: random legal, always pass on challenge/block ----

function easyAction(actions: number[]): number {
  if (actions.includes(ACTION_PASS)) return ACTION_PASS;
  return actions[Math.floor(Math.random() * actions.length)];
}

// ---- Medium: honest rule-based play ----

function mediumAction(actions: number[], seat: number, wasm: CoupWasm): number {
  const myCards = getOwnCards(seat, wasm);
  const myCoins = wasm.playerCoins(seat);

  // Discard phase: lose the least valuable card
  if (actions.some(a => a >= ACTION_DISCARD_SLOT0 && a <= ACTION_DISCARD_SLOT3)) {
    return chooseLoseCard(actions, myCards, seat, wasm);
  }

  // Challenge/Block phase
  if (actions.includes(ACTION_CHALLENGE) || actions.includes(ACTION_PASS)) {
    return handleChallengeBlock(actions, myCards);
  }

  // Main action phase
  return chooseMainAction(actions, myCards, myCoins, seat, wasm);
}

function chooseMainAction(
  actions: number[],
  myCards: number[],
  myCoins: number,
  seat: number,
  wasm: CoupWasm,
): number {
  const coupTargets = actions.filter(a => a >= ACTION_COUP_P0 && a <= ACTION_COUP_P0 + 5);

  // Must coup at 10+
  if (myCoins >= 10 && coupTargets.length > 0) {
    return pickTarget(coupTargets, ACTION_COUP_P0, wasm, "strongest");
  }

  // Coup at 7+ targeting strongest
  if (myCoins >= 7 && coupTargets.length > 0) {
    return pickTarget(coupTargets, ACTION_COUP_P0, wasm, "strongest");
  }

  // Tax if holding Duke
  if (myCards.includes(DUKE) && actions.includes(ACTION_TAX)) {
    return ACTION_TAX;
  }

  // Exchange if holding Ambassador
  if (myCards.includes(AMBASSADOR) && actions.includes(ACTION_EXCHANGE)) {
    return ACTION_EXCHANGE;
  }

  // Steal if holding Captain
  if (myCards.includes(CAPTAIN)) {
    const stealTargets = actions.filter(a => a >= ACTION_STEAL_P0 && a <= ACTION_STEAL_P0 + 5);
    if (stealTargets.length > 0) {
      return pickTarget(stealTargets, ACTION_STEAL_P0, wasm, "richest");
    }
  }

  // Assassinate if holding Assassin and can afford
  if (myCards.includes(ASSASSIN) && myCoins >= 3) {
    const assTargets = actions.filter(a => a >= ACTION_ASSASSINATE_P0 && a <= ACTION_ASSASSINATE_P0 + 5);
    if (assTargets.length > 0) {
      return pickTarget(assTargets, ACTION_ASSASSINATE_P0, wasm, "weakest");
    }
  }

  // Foreign Aid as fallback
  if (actions.includes(ACTION_FOREIGN_AID)) return ACTION_FOREIGN_AID;

  // Income as last resort
  if (actions.includes(ACTION_INCOME)) return ACTION_INCOME;

  return actions[0];
}

function handleChallengeBlock(actions: number[], myCards: number[]): number {
  // Block if holding the correct card (honest play)
  if (actions.includes(ACTION_BLOCK_CONTESSA) && myCards.includes(CONTESSA)) {
    return ACTION_BLOCK_CONTESSA;
  }
  if (actions.includes(ACTION_BLOCK_CAPTAIN) && myCards.includes(CAPTAIN)) {
    return ACTION_BLOCK_CAPTAIN;
  }
  if (actions.includes(ACTION_BLOCK_AMBASSADOR) && myCards.includes(AMBASSADOR)) {
    return ACTION_BLOCK_AMBASSADOR;
  }
  if (actions.includes(ACTION_BLOCK_DUKE) && myCards.includes(DUKE)) {
    return ACTION_BLOCK_DUKE;
  }

  // Medium never challenges
  return ACTION_PASS;
}

function chooseLoseCard(
  actions: number[],
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
): number {
  const cardValue: Record<number, number> = {
    [DUKE]: 5,
    [ASSASSIN]: 3,
    [CAPTAIN]: 4,
    [AMBASSADOR]: 2,
    [CONTESSA]: 1,
  };

  const discardActions = actions.filter(a => a >= ACTION_DISCARD_SLOT0 && a <= ACTION_DISCARD_SLOT3);

  // For exchange discard (slots 2-3 are the drawn cards), prefer discarding drawn cards
  // For lose_card, lose the least valuable
  let bestAction = discardActions[0];
  let bestValue = Infinity;

  for (const a of discardActions) {
    const slot = a - ACTION_DISCARD_SLOT0;
    let cardType: number;
    if (slot === 0) cardType = wasm.playerCard0Type(seat);
    else if (slot === 1) cardType = wasm.playerCard1Type(seat);
    else if (slot === 2) cardType = wasm.exchangeCard0();
    else cardType = wasm.exchangeCard1();

    const v = cardValue[cardType] ?? 0;
    if (v < bestValue) {
      bestValue = v;
      bestAction = a;
    }
  }
  return bestAction;
}

function pickTarget(
  targetActions: number[],
  baseAction: number,
  wasm: CoupWasm,
  strategy: "strongest" | "weakest" | "richest",
): number {
  let best = targetActions[0];
  let bestScore = strategy === "weakest" ? Infinity : -1;

  for (const a of targetActions) {
    const targetSeat = a - baseAction;
    const coins = wasm.playerCoins(targetSeat);
    if ((strategy === "strongest" || strategy === "richest") && coins > bestScore) {
      bestScore = coins;
      best = a;
    } else if (strategy === "weakest" && coins < bestScore) {
      bestScore = coins;
      best = a;
    }
  }
  return best;
}

function getOwnCards(seat: number, wasm: CoupWasm): number[] {
  const cards: number[] = [];
  if (wasm.playerCard0Alive(seat)) cards.push(wasm.playerCard0Type(seat));
  if (wasm.playerCard1Alive(seat)) cards.push(wasm.playerCard1Type(seat));
  return cards;
}
