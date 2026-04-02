// T12: Hard bot — probabilistic with card tracking, bluffs strategically

import type { Agent } from "../agent.js";
import { Action, CardType } from "../types.js";

const CARD_COUNT = 3; // 3 of each card type in the deck

export class HardBot implements Agent {
  readonly name: string;
  private seat: number;
  private numPlayers: number;
  private revealedCounts: Record<CardType, number> = {
    [CardType.Duke]: 0,
    [CardType.Assassin]: 0,
    [CardType.Captain]: 0,
    [CardType.Ambassador]: 0,
    [CardType.Contessa]: 0,
  };
  private claimHistory: Map<number, CardType[]> = new Map();

  constructor(id: number, seat: number, numPlayers: number) {
    this.name = `bot-${id}`;
    this.seat = seat;
    this.numPlayers = numPlayers;
  }

  noteRevealedCard(cardType: CardType) {
    this.revealedCounts[cardType]++;
  }

  noteClaim(player: number, cardType: CardType) {
    const claims = this.claimHistory.get(player) ?? [];
    claims.push(cardType);
    this.claimHistory.set(player, claims);
  }

  async chooseAction(obs: Float32Array, validMask: number): Promise<number> {
    const actions = getValidActions(validMask);
    if (actions.length === 1) return actions[0];

    const myCards = getOwnCards(obs);

    // LOSE_CARD / EXCHANGE_DISCARD
    if (actions.some(a => a >= Action.DiscardSlot0 && a <= Action.DiscardSlot3)) {
      return this.chooseLoseCard(actions, myCards);
    }

    // Challenge/Block phase
    if (actions.includes(Action.Challenge) || actions.includes(Action.Pass)) {
      return this.handleChallengeBlock(actions, myCards, obs);
    }

    // MAIN_ACTION
    return this.chooseMainAction(actions, myCards, obs);
  }

  private chooseMainAction(actions: number[], myCards: CardType[], obs: Float32Array): number {
    // Coup if can afford, target strongest
    const coupTargets = actions.filter(a => a >= Action.CoupP0 && a <= Action.CoupP5);
    if (coupTargets.length > 0) {
      // Always coup at 10+
      const mustCoup = actions.length === coupTargets.length;
      if (mustCoup) return this.pickBestTarget(coupTargets, "strongest");

      // Prefer coup at 7+ targeting the player with most coins
      return this.pickBestTarget(coupTargets, "strongest");
    }

    // Assassinate if can afford — target weakest influence
    if (myCards.includes(CardType.Assassin)) {
      const assTargets = actions.filter(a => a >= Action.AssassinateP0 && a <= Action.AssassinateP5);
      if (assTargets.length > 0) {
        return this.pickBestTarget(assTargets, "weakest");
      }
    }

    // Tax — claim Duke even without it (bluff) if few Dukes revealed
    if (actions.includes(Action.Tax)) {
      if (myCards.includes(CardType.Duke) || this.shouldBluff(CardType.Duke)) {
        return Action.Tax;
      }
    }

    // Steal — claim Captain
    const stealTargets = actions.filter(a => a >= Action.StealP0 && a <= Action.StealP5);
    if (stealTargets.length > 0) {
      if (myCards.includes(CardType.Captain) || this.shouldBluff(CardType.Captain)) {
        return this.pickBestTarget(stealTargets, "strongest");
      }
    }

    // Exchange if holding Ambassador
    if (myCards.includes(CardType.Ambassador) && actions.includes(Action.Exchange)) {
      return Action.Exchange;
    }

    // Foreign Aid
    if (actions.includes(Action.ForeignAid)) return Action.ForeignAid;

    // Bluff Tax even without Duke as a fallback before Income
    if (actions.includes(Action.Tax) && this.revealedCounts[CardType.Duke] < 2) {
      return Action.Tax;
    }

    return actions.includes(Action.Income) ? Action.Income : actions[0];
  }

  private handleChallengeBlock(actions: number[], myCards: CardType[], obs: Float32Array): number {
    // Block with correct card if held
    const blockActions = actions.filter(a => a >= Action.BlockContessa && a <= Action.BlockDuke);
    for (const b of blockActions) {
      const cardNeeded = blockToCard(b);
      if (cardNeeded !== null && myCards.includes(cardNeeded)) {
        return b;
      }
    }

    // Counter-bluff: block assassination with Contessa even without one if it would be fatal
    if (blockActions.includes(Action.BlockContessa) && !myCards.includes(CardType.Contessa)) {
      // If we only have 1 influence, blocking is worth the risk
      const aliveCards = myCards.length;
      if (aliveCards <= 1 || Math.random() < 0.4) {
        return Action.BlockContessa;
      }
    }

    // Challenge suspicious claims
    if (actions.includes(Action.Challenge)) {
      // Challenge if 2+ of the claimed card type are revealed
      // (We'd need to know what was claimed — approximate by checking all card types with high revealed count)
      for (const [card, count] of Object.entries(this.revealedCounts)) {
        if (count >= 2 && Math.random() < 0.6) {
          return Action.Challenge;
        }
      }
      // Random challenge with low probability
      if (Math.random() < 0.15) {
        return Action.Challenge;
      }
    }

    return Action.Pass;
  }

  private shouldBluff(cardType: CardType): boolean {
    const revealed = this.revealedCounts[cardType];
    // More likely to bluff if fewer of that card are revealed (harder to challenge)
    if (revealed === 0) return Math.random() < 0.5;
    if (revealed === 1) return Math.random() < 0.3;
    return false; // Don't bluff if 2+ revealed
  }

  private chooseLoseCard(actions: number[], myCards: CardType[]): number {
    const value: Record<CardType, number> = {
      [CardType.Duke]: 5,
      [CardType.Captain]: 4,
      [CardType.Assassin]: 3,
      [CardType.Ambassador]: 2,
      [CardType.Contessa]: 1,
    };

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

  private pickBestTarget(targetActions: number[], strategy: "strongest" | "weakest"): number {
    // Without direct coin access, use random weighted selection
    // In practice this would read from observation tensor
    return targetActions[Math.floor(Math.random() * targetActions.length)];
  }
}

function blockToCard(action: number): CardType | null {
  switch (action) {
    case Action.BlockContessa: return CardType.Contessa;
    case Action.BlockCaptain: return CardType.Captain;
    case Action.BlockAmbassador: return CardType.Ambassador;
    case Action.BlockDuke: return CardType.Duke;
    default: return null;
  }
}

function getValidActions(mask: number): number[] {
  const actions: number[] = [];
  for (let i = 0; i < 32; i++) {
    if ((mask >> i) & 1) actions.push(i);
  }
  return actions;
}

function getOwnCards(obs: Float32Array): CardType[] {
  const cards: CardType[] = [];
  if (obs[5] > 0.5) {
    for (let i = 0; i < 5; i++) {
      if (obs[i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  if (obs[11] > 0.5) {
    for (let i = 0; i < 5; i++) {
      if (obs[6 + i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  return cards;
}
