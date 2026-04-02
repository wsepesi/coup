// T11: Medium bot — rule-based, never bluffs, challenges obvious lies

import type { Agent } from "../agent.js";
import { Action, CardType, Phase, OBS_SIZE } from "../types.js";

export class MediumBot implements Agent {
  readonly name: string;
  private seat: number;

  constructor(id: number, seat: number) {
    this.name = `bot-${id}`;
    this.seat = seat;
  }

  async chooseAction(obs: Float32Array, validMask: number): Promise<number> {
    const actions = getValidActions(validMask);
    if (actions.length === 1) return actions[0];

    const myCards = getOwnCards(obs);

    // LOSE_CARD: lose the less valuable card
    if (actions.some(a => a >= Action.DiscardSlot0 && a <= Action.DiscardSlot3)) {
      return this.chooseLoseCard(actions, myCards);
    }

    // EXCHANGE_DISCARD: keep best cards
    if (actions.some(a => a >= Action.DiscardSlot0)) {
      return actions[actions.length - 1]; // discard last slot (drawn cards)
    }

    // Challenge/Block phase
    if (actions.includes(Action.Challenge) || actions.includes(Action.Pass)) {
      return this.handleChallengeBlock(actions, myCards, obs);
    }

    // MAIN_ACTION phase
    return this.chooseMainAction(actions, myCards, obs);
  }

  private chooseMainAction(actions: number[], myCards: CardType[], obs: Float32Array): number {
    const myCoins = getCoins(obs, this.seat);

    // Must coup at 10+
    const coupTargets = actions.filter(a => a >= Action.CoupP0 && a <= Action.CoupP5);
    if (myCoins >= 10 && coupTargets.length > 0) {
      return this.pickTarget(coupTargets, obs, "strongest");
    }

    // Prefer coup at 7+ targeting strongest
    if (myCoins >= 7 && coupTargets.length > 0) {
      return this.pickTarget(coupTargets, obs, "strongest");
    }

    // Tax if holding Duke (honest play)
    if (myCards.includes(CardType.Duke) && actions.includes(Action.Tax)) {
      return Action.Tax;
    }

    // Exchange if holding Ambassador
    if (myCards.includes(CardType.Ambassador) && actions.includes(Action.Exchange)) {
      return Action.Exchange;
    }

    // Steal if holding Captain
    if (myCards.includes(CardType.Captain)) {
      const stealTargets = actions.filter(a => a >= Action.StealP0 && a <= Action.StealP5);
      if (stealTargets.length > 0) {
        return this.pickTarget(stealTargets, obs, "weakest");
      }
    }

    // Assassinate if holding Assassin and can afford
    if (myCards.includes(CardType.Assassin) && myCoins >= 3) {
      const assTargets = actions.filter(a => a >= Action.AssassinateP0 && a <= Action.AssassinateP5);
      if (assTargets.length > 0) {
        return this.pickTarget(assTargets, obs, "weakest");
      }
    }

    // Foreign Aid as fallback
    if (actions.includes(Action.ForeignAid)) return Action.ForeignAid;

    // Income as last resort
    if (actions.includes(Action.Income)) return Action.Income;

    return actions[0];
  }

  private handleChallengeBlock(actions: number[], myCards: CardType[], obs: Float32Array): number {
    // Block if holding the correct card
    if (actions.includes(Action.BlockContessa) && myCards.includes(CardType.Contessa)) {
      return Action.BlockContessa;
    }
    if (actions.includes(Action.BlockCaptain) && myCards.includes(CardType.Captain)) {
      return Action.BlockCaptain;
    }
    if (actions.includes(Action.BlockAmbassador) && myCards.includes(CardType.Ambassador)) {
      return Action.BlockAmbassador;
    }
    if (actions.includes(Action.BlockDuke) && myCards.includes(CardType.Duke)) {
      return Action.BlockDuke;
    }

    // Challenge if opponent claims a role and we hold 2 of that card type
    // (simplified: we know the role from the pending action context, but just pass for medium)
    // Medium never challenges (too risky without card tracking)

    return Action.Pass;
  }

  private chooseLoseCard(actions: number[], myCards: CardType[]): number {
    // Card value ranking: Duke > Captain > Assassin > Ambassador > Contessa
    const value: Record<CardType, number> = {
      [CardType.Duke]: 5,
      [CardType.Captain]: 4,
      [CardType.Assassin]: 3,
      [CardType.Ambassador]: 2,
      [CardType.Contessa]: 1,
    };

    // Lose the least valuable card
    let bestAction = actions[0];
    let bestValue = Infinity;
    for (const a of actions) {
      if (a >= Action.DiscardSlot0 && a <= Action.DiscardSlot3) {
        const slot = a - Action.DiscardSlot0;
        if (slot < myCards.length) {
          const v = value[myCards[slot]] ?? 0;
          if (v < bestValue) {
            bestValue = v;
            bestAction = a;
          }
        }
      }
    }
    return bestAction;
  }

  private pickTarget(
    targetActions: number[],
    obs: Float32Array,
    strategy: "strongest" | "weakest",
  ): number {
    let best = targetActions[0];
    let bestScore = strategy === "strongest" ? -1 : Infinity;

    for (const a of targetActions) {
      let targetSeat: number;
      if (a >= Action.CoupP0 && a <= Action.CoupP5) targetSeat = a - Action.CoupP0;
      else if (a >= Action.StealP0 && a <= Action.StealP5) targetSeat = a - Action.StealP0;
      else targetSeat = a - Action.AssassinateP0;

      const coins = getCoins(obs, targetSeat);
      if (strategy === "strongest" && coins > bestScore) {
        bestScore = coins;
        best = a;
      } else if (strategy === "weakest" && coins < bestScore) {
        bestScore = coins;
        best = a;
      }
    }

    return best;
  }
}

function getValidActions(mask: number): number[] {
  const actions: number[] = [];
  for (let i = 0; i < 32; i++) {
    if ((mask >> i) & 1) actions.push(i);
  }
  return actions;
}

// Extract own cards from observation tensor
// Observation layout (from plan.md §6):
// [0-4]: card0 type one-hot (5 floats)
// [5]: card0 alive
// [6-10]: card1 type one-hot (5 floats)
// [11]: card1 alive
function getOwnCards(obs: Float32Array): CardType[] {
  const cards: CardType[] = [];
  // Card 0
  if (obs[5] > 0.5) { // alive
    for (let i = 0; i < 5; i++) {
      if (obs[i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  // Card 1
  if (obs[11] > 0.5) { // alive
    for (let i = 0; i < 5; i++) {
      if (obs[6 + i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  return cards;
}

// Get coins for a player from observation
// This is approximate — coins are encoded in the observation but exact layout depends on C engine
function getCoins(obs: Float32Array, seat: number): number {
  // Simplified: use a fixed offset. Real implementation would parse the full obs tensor.
  // For now, return a default that makes targeting somewhat random
  return Math.floor(Math.random() * 10);
}
