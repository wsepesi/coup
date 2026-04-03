// Heuristic bots for the Coup Workers backend.
// Easy = random legal action (always passes on challenges/blocks).
// Medium = rule-based honest play, never bluffs, blocks when holding the card.
//
// SECURITY NOTE: Bots receive an unrestricted CoupWasm instance and can read ANY
// player's cards via playerCard0Type/playerCard1Type. All card reads MUST be guarded:
// only read own seat, or check !playerCardXAlive(p) before reading dead cards.
// If bot logic grows more complex, add a FilteredWasm wrapper to enforce this.

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
  if (difficulty === "hard") {
    return hardAction(actions, seat, wasm);
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
      return pickTarget(stealTargets, ACTION_STEAL_P0, wasm, "weakest");
    }
  }

  // Assassinate if holding Assassin and can afford
  if (myCards.includes(ASSASSIN) && myCoins >= 3) {
    const assTargets = actions.filter(a => a >= ACTION_ASSASSINATE_P0 && a <= ACTION_ASSASSINATE_P0 + 5);
    if (assTargets.length > 0) {
      return pickTarget(assTargets, ACTION_ASSASSINATE_P0, wasm, "weakest");
    }
  }

  // Foreign Aid as fallback — but only if all 3 Dukes are known dead (face-up),
  // otherwise someone could block with a Duke
  const revDukes = countRevealedDukes(seat, wasm);
  if (actions.includes(ACTION_FOREIGN_AID) && revDukes >= 3) {
    console.warn(`[Bot] seat=${seat} medium chose Foreign Aid (revealed dukes: ${revDukes})`);
    return ACTION_FOREIGN_AID;
  }

  // Income as last resort
  if (actions.includes(ACTION_INCOME)) {
    console.warn(`[Bot] seat=${seat} medium fell through to Income (cards: [${myCards}], coins: ${myCoins}, actions: [${actions}])`);
    return ACTION_INCOME;
  }

  console.warn(`[Bot] seat=${seat} medium fallthrough to actions[0]=${actions[0]} (cards: [${myCards}], coins: ${myCoins}, actions: [${actions}])`);
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

function countRevealedDukes(seat: number, wasm: CoupWasm): number {
  let count = 0;
  const numPlayers = wasm.getNumPlayers();
  for (let p = 0; p < numPlayers; p++) {
    // Dead cards are visible to all — check if dead card is a Duke
    if (!wasm.playerCard0Alive(p) && wasm.playerCard0Type(p) === DUKE) count++;
    if (!wasm.playerCard1Alive(p) && wasm.playerCard1Type(p) === DUKE) count++;
  }
  return count;
}

function getOwnCards(seat: number, wasm: CoupWasm): number[] {
  const cards: number[] = [];
  if (wasm.playerCard0Alive(seat)) cards.push(wasm.playerCard0Type(seat));
  if (wasm.playerCard1Alive(seat)) cards.push(wasm.playerCard1Type(seat));
  return cards;
}

// ---- Hard: probabilistic with card tracking, bluffs strategically ----
// Stateless port of packages/game-client/src/agents/heuristic-hard.ts
// Uses direct WASM calls instead of obs tensor. DEFAULT_PROFILE (all 0.5).

const CARD_COUNT = 3; // 3 of each card type in the deck
const ALL_CARD_TYPES = [DUKE, ASSASSIN, CAPTAIN, AMBASSADOR, CONTESSA];

function countAllRevealed(wasm: CoupWasm): Record<number, number> {
  const counts: Record<number, number> = {
    [DUKE]: 0, [ASSASSIN]: 0, [CAPTAIN]: 0, [AMBASSADOR]: 0, [CONTESSA]: 0,
  };
  const numPlayers = wasm.getNumPlayers();
  for (let p = 0; p < numPlayers; p++) {
    if (!wasm.playerCard0Alive(p)) counts[wasm.playerCard0Type(p)]++;
    if (!wasm.playerCard1Alive(p)) counts[wasm.playerCard1Type(p)]++;
  }
  return counts;
}

function countAliveOpponents(seat: number, wasm: CoupWasm): number {
  let count = 0;
  const numPlayers = wasm.getNumPlayers();
  for (let p = 0; p < numPlayers; p++) {
    if (p !== seat && wasm.playerIsAlive(p)) count++;
  }
  return count;
}

function getPendingActionFromWasm(wasm: CoupWasm): number {
  return wasm.getPendingAction();
}

function getTurnPlayerFromWasm(wasm: CoupWasm): number {
  return wasm.getTurnPlayer();
}

function blockToCard(action: number): number | null {
  if (action === ACTION_BLOCK_CONTESSA) return CONTESSA;
  if (action === ACTION_BLOCK_CAPTAIN) return CAPTAIN;
  if (action === ACTION_BLOCK_AMBASSADOR) return AMBASSADOR;
  if (action === ACTION_BLOCK_DUKE) return DUKE;
  return null;
}

function hardAction(actions: number[], seat: number, wasm: CoupWasm): number {
  const myCards = getOwnCards(seat, wasm);
  const revealed = countAllRevealed(wasm);

  // LOSE_CARD / EXCHANGE_DISCARD
  if (actions.some(a => a >= ACTION_DISCARD_SLOT0 && a <= ACTION_DISCARD_SLOT3)) {
    return hardChooseLoseCard(actions, myCards, seat, wasm, revealed);
  }

  // Challenge/Block phase
  if (actions.includes(ACTION_CHALLENGE) || actions.includes(ACTION_PASS)) {
    return hardChallengeBlock(actions, myCards, seat, wasm, revealed);
  }

  // Main action phase
  return hardMainAction(actions, myCards, seat, wasm, revealed);
}

function hardMainAction(
  actions: number[],
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
  revealed: Record<number, number>,
): number {
  const myCoins = wasm.playerCoins(seat);

  // Coup targets
  const coupTargets = actions.filter(a => a >= ACTION_COUP_P0 && a <= ACTION_COUP_P0 + 5);
  if (coupTargets.length > 0) {
    const mustCoup = actions.length === coupTargets.length;
    const coupThreshold = 8; // greed=0.5 → 7 + round(0.5) = 8
    if (mustCoup || myCoins >= coupThreshold) {
      return hardPickTarget(coupTargets, ACTION_COUP_P0, wasm, "strongest");
    }
  }

  // Assassinate if holding Assassin
  if (myCards.includes(ASSASSIN)) {
    const assTargets = actions.filter(a => a >= ACTION_ASSASSINATE_P0 && a <= ACTION_ASSASSINATE_P0 + 5);
    if (assTargets.length > 0) {
      return hardPickTarget(assTargets, ACTION_ASSASSINATE_P0, wasm, "weakest");
    }
  }

  // Tax — claim Duke even without it (bluff) if favorable
  if (actions.includes(ACTION_TAX)) {
    if (myCards.includes(DUKE) || shouldBluff(DUKE, myCards, seat, wasm, revealed)) {
      return ACTION_TAX;
    }
  }

  // Steal — claim Captain
  const stealTargets = actions.filter(a => a >= ACTION_STEAL_P0 && a <= ACTION_STEAL_P0 + 5);
  if (stealTargets.length > 0) {
    if (myCards.includes(CAPTAIN) || shouldBluff(CAPTAIN, myCards, seat, wasm, revealed)) {
      return hardPickTarget(stealTargets, ACTION_STEAL_P0, wasm, "strongest");
    }
  }

  // Exchange if holding Ambassador
  if (myCards.includes(AMBASSADOR) && actions.includes(ACTION_EXCHANGE)) {
    return ACTION_EXCHANGE;
  }

  // Always claim Tax over Income — unless all Dukes are revealed
  if (actions.includes(ACTION_TAX) && revealed[DUKE] < CARD_COUNT) {
    return ACTION_TAX;
  }

  // Foreign Aid only when all Dukes are revealed
  if (actions.includes(ACTION_FOREIGN_AID) && revealed[DUKE] >= CARD_COUNT) {
    console.warn(`[Bot] seat=${seat} hard chose Foreign Aid (revealed dukes: ${revealed[DUKE]})`);
    return ACTION_FOREIGN_AID;
  }

  if (actions.includes(ACTION_INCOME)) {
    console.warn(`[Bot] seat=${seat} hard fell through to Income (cards: [${myCards}], revealed: ${JSON.stringify(revealed)}, actions: [${actions}])`);
    return ACTION_INCOME;
  }
  console.warn(`[Bot] seat=${seat} hard fallthrough to actions[0]=${actions[0]} (cards: [${myCards}], actions: [${actions}])`);
  return actions[0];
}

function hardChallengeBlock(
  actions: number[],
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
  revealed: Record<number, number>,
): number {
  // Block with correct card if held
  const blockActions = actions.filter(a => a >= ACTION_BLOCK_CONTESSA && a <= ACTION_BLOCK_DUKE);
  for (const b of blockActions) {
    const cardNeeded = blockToCard(b);
    if (cardNeeded !== null && myCards.includes(cardNeeded)) {
      return b;
    }
  }

  // Counter-bluff: block assassination with Contessa even without one
  if (blockActions.includes(ACTION_BLOCK_CONTESSA) && !myCards.includes(CONTESSA)) {
    const aliveCards = myCards.length;

    // Always bluff-block if it would be fatal (1 influence left)
    if (aliveCards <= 1) {
      return ACTION_BLOCK_CONTESSA;
    }
    // bluffTolerance=0.5: prob = 0.15 + 0.5*0.4 = 0.35
    let bluffProb = 0.35;
    if (revealed[CONTESSA] === 0) bluffProb += 0.1;
    if (Math.random() < bluffProb) {
      return ACTION_BLOCK_CONTESSA;
    }
  }

  // Bluff-block steal with Captain/Ambassador even without holding them
  for (const b of blockActions) {
    if (b === ACTION_BLOCK_CONTESSA) continue;
    const cardNeeded = blockToCard(b);
    if (cardNeeded !== null && !myCards.includes(cardNeeded)) {
      let prob = 0.5 * 0.35; // bluffTolerance=0.5 → 0.175
      if (revealed[cardNeeded] === 0) prob += 0.1;
      if (Math.random() < prob) {
        return b;
      }
    }
  }

  // Challenge
  if (actions.includes(ACTION_CHALLENGE)) {
    const pendingAction = getPendingActionFromWasm(wasm);
    const claimedCard = claimedRoleForAction(pendingAction);

    // challengeRate=0.5 → cr = 1.0
    const cr = 1.0;

    if (claimedCard !== null) {
      const revCount = revealed[claimedCard];

      // All copies revealed → guaranteed win
      if (revCount >= CARD_COUNT) {
        return ACTION_CHALLENGE;
      }

      // 2 revealed → very likely bluffing
      if (revCount >= 2) {
        if (Math.random() < 0.8 * cr) return ACTION_CHALLENGE;
      }

      // 1 revealed → moderate challenge chance
      if (revCount === 1 && Math.random() < 0.2 * cr) {
        return ACTION_CHALLENGE;
      }
    }

    // Baseline random challenge
    if (Math.random() < 0.1 * cr) {
      return ACTION_CHALLENGE;
    }
  }

  return ACTION_PASS;
}

function claimedRoleForAction(action: number): number | null {
  if (action === ACTION_TAX) return DUKE;
  if (action === ACTION_EXCHANGE) return AMBASSADOR;
  if (action >= ACTION_STEAL_P0 && action <= ACTION_STEAL_P0 + 5) return CAPTAIN;
  if (action >= ACTION_ASSASSINATE_P0 && action <= ACTION_ASSASSINATE_P0 + 5) return ASSASSIN;
  // Block claims
  if (action === ACTION_BLOCK_CONTESSA) return CONTESSA;
  if (action === ACTION_BLOCK_CAPTAIN) return CAPTAIN;
  if (action === ACTION_BLOCK_AMBASSADOR) return AMBASSADOR;
  if (action === ACTION_BLOCK_DUKE) return DUKE;
  return null;
}

function shouldBluff(
  cardType: number,
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
  revealed: Record<number, number>,
): boolean {
  const revCount = revealed[cardType];
  if (revCount >= 2) return false;

  // bluffTolerance=0.5
  let prob: number;
  if (revCount === 0) prob = 0.2 + 0.5 * 0.4; // 0.4
  else prob = 0.1 + 0.5 * 0.2; // 0.2

  // Context: bluff more when desperate
  const myCoins = wasm.playerCoins(seat);
  const myInfluence = myCards.length;

  if (myCoins <= 1 && myInfluence <= 1) prob += 0.25;
  else if (myCoins <= 2) prob += 0.1;

  if (myCoins >= 6 && myInfluence >= 2) prob -= 0.15;

  const aliveOpp = countAliveOpponents(seat, wasm);
  if (aliveOpp <= 1 && myInfluence >= 2) prob -= 0.1;

  return Math.random() < Math.max(0, Math.min(1, prob));
}

function hardChooseLoseCard(
  actions: number[],
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
  revealed: Record<number, number>,
): number {
  const value = hardCardValues(myCards, seat, wasm);

  const discardActions = actions.filter(a => a >= ACTION_DISCARD_SLOT0 && a <= ACTION_DISCARD_SLOT3);
  let bestAction = discardActions[0];
  let bestValue = Infinity;

  for (const a of discardActions) {
    const slot = a - ACTION_DISCARD_SLOT0;
    let cardType: number;
    if (slot === 0) cardType = wasm.playerCard0Type(seat);
    else if (slot === 1) cardType = wasm.playerCard1Type(seat);
    else if (slot === 2) cardType = wasm.exchangeCard0();
    else cardType = wasm.exchangeCard1();

    const v = value[cardType] ?? 0;
    if (v < bestValue) {
      bestValue = v;
      bestAction = a;
    }
  }
  return bestAction;
}

function hardCardValues(
  myCards: number[],
  seat: number,
  wasm: CoupWasm,
): Record<number, number> {
  const base: Record<number, number> = {
    [DUKE]: 5,
    [CAPTAIN]: 4,
    [ASSASSIN]: 3,
    [AMBASSADOR]: 2,
    [CONTESSA]: 1,
  };

  let anyCanAssassinate = false;
  let maxOpponentCoins = 0;
  let allOpponentsBroke = true;
  const numPlayers = wasm.getNumPlayers();

  for (let p = 0; p < numPlayers; p++) {
    if (p === seat) continue;
    if (!wasm.playerIsAlive(p)) continue;
    const coins = wasm.playerCoins(p);
    if (coins >= 3) anyCanAssassinate = true;
    if (coins > maxOpponentCoins) maxOpponentCoins = coins;
    if (coins >= 2) allOpponentsBroke = false;
  }

  if (anyCanAssassinate) base[CONTESSA] += 3;
  if (allOpponentsBroke) base[CAPTAIN] -= 2;
  else if (maxOpponentCoins >= 5) base[CAPTAIN] += 1;

  const myCoins = wasm.playerCoins(seat);
  if (myCoins < 2) base[ASSASSIN] -= 1;

  const aliveOpp = countAliveOpponents(seat, wasm);
  if (aliveOpp >= 3) base[AMBASSADOR] += 1;

  return base;
}

function hardPickTarget(
  targetActions: number[],
  baseAction: number,
  wasm: CoupWasm,
  strategy: "strongest" | "weakest",
): number {
  let bestAction = targetActions[0];
  let bestScore = strategy === "strongest" ? -1 : Infinity;

  for (const a of targetActions) {
    const targetSeat = a - baseAction;
    const coins = wasm.playerCoins(targetSeat);
    const influence = (wasm.playerCard0Alive(targetSeat) ? 1 : 0) + (wasm.playerCard1Alive(targetSeat) ? 1 : 0);

    if (strategy === "strongest") {
      const score = coins * 10 + influence;
      if (score > bestScore) { bestScore = score; bestAction = a; }
    } else {
      const score = influence * 100 + coins;
      if (score < bestScore) { bestScore = score; bestAction = a; }
    }
  }
  return bestAction;
}
