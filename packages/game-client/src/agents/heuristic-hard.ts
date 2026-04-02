// T12: Hard bot — probabilistic with card tracking, bluffs strategically
// Supports optional CharacterProfile for personality variation (hard+ mode)

import type { Agent } from "../agent.js";
import { Action, CardType, claimedRole } from "../types.js";

const CARD_COUNT = 3; // 3 of each card type in the deck
const MAX_PLAYERS = 6;

// Obs tensor offsets
const OBS_CARDS_BASE = 0;       // 6 players × 12 floats
const OBS_COINS_BASE = 72;      // 6 floats (normalized by /12)
const OBS_ALIVE_BASE = 78;      // 6 floats
const OBS_PENDING_BASE = 103;   // 32 floats one-hot
const OBS_TURN_PLAYER_BASE = 97; // 6 floats one-hot

/** All values 0-1. Omit for fixed hard bot (defaults to 0.5 across the board). */
export interface CharacterProfile {
  /** Preference for offensive actions — assassinate/steal/early coup */
  aggression: number;
  /** Willingness to bluff and counter-bluff */
  bluffTolerance: number;
  /** How readily they challenge other players' claims */
  challengeRate: number;
  /** Tendency to retaliate against whoever last targeted them */
  spite: number;
  /** Preference for hoarding coins vs spending them */
  greed: number;
}

const DEFAULT_PROFILE: CharacterProfile = {
  aggression: 0.5,
  bluffTolerance: 0.5,
  challengeRate: 0.5,
  spite: 0.5,
  greed: 0.5,
};

export function randomProfile(rng: () => number): CharacterProfile {
  return {
    aggression: rng(),
    bluffTolerance: rng(),
    challengeRate: rng(),
    spite: rng(),
    greed: rng(),
  };
}

export class HardBot implements Agent {
  readonly name: string;
  private seat: number;
  private numPlayers: number;
  private p: CharacterProfile;
  private revealedCounts: Record<CardType, number> = {
    [CardType.Duke]: 0,
    [CardType.Assassin]: 0,
    [CardType.Captain]: 0,
    [CardType.Ambassador]: 0,
    [CardType.Contessa]: 0,
  };
  private claimHistory: Map<number, Set<CardType>> = new Map();
  /** Seat of the last player who targeted us (for spite) */
  private lastAttacker: number | null = null;

  constructor(name: string, seat: number, numPlayers: number, profile?: CharacterProfile) {
    this.name = name;
    this.seat = seat;
    this.numPlayers = numPlayers;
    this.p = profile ?? DEFAULT_PROFILE;
  }

  noteRevealedCard(cardType: CardType) {
    this.revealedCounts[cardType]++;
  }

  noteClaim(player: number, cardType: CardType) {
    if (!this.claimHistory.has(player)) {
      this.claimHistory.set(player, new Set());
    }
    this.claimHistory.get(player)!.add(cardType);
  }

  noteTargeted(byPlayer: number) {
    this.lastAttacker = byPlayer;
  }

  async chooseAction(obs: Float32Array, validMask: number): Promise<number> {
    const actions = getValidActions(validMask);
    if (actions.length === 1) return actions[0];

    const myCards = getOwnCards(obs, this.seat);

    // LOSE_CARD / EXCHANGE_DISCARD
    if (actions.some(a => a >= Action.DiscardSlot0 && a <= Action.DiscardSlot3)) {
      return this.chooseLoseCard(actions, myCards, obs);
    }

    // Challenge/Block phase
    if (actions.includes(Action.Challenge) || actions.includes(Action.Pass)) {
      return this.handleChallengeBlock(actions, myCards, obs);
    }

    // MAIN_ACTION
    return this.chooseMainAction(actions, myCards, obs);
  }

  private chooseMainAction(actions: number[], myCards: CardType[], obs: Float32Array): number {
    const myCoins = getPlayerCoins(obs, this.seat);

    // Coup if can afford, target strongest (or spite target)
    const coupTargets = actions.filter(a => a >= Action.CoupP0 && a <= Action.CoupP5);
    if (coupTargets.length > 0) {
      const mustCoup = actions.length === coupTargets.length;
      // Greedy bots delay coup slightly (7 vs 8), but never past 8 — sitting at 9 is always bad
      const coupThreshold = 7 + Math.round(this.p.greed); // 7 or 8
      if (mustCoup || myCoins >= coupThreshold) {
        return this.pickBestTarget(coupTargets, "strongest", obs);
      }
    }

    // Assassinate if holding Assassin — always correct, aggression only shifts priority vs Tax
    if (myCards.includes(CardType.Assassin)) {
      const assTargets = actions.filter(a => a >= Action.AssassinateP0 && a <= Action.AssassinateP5);
      if (assTargets.length > 0) {
        return this.pickBestTarget(assTargets, "weakest", obs);
      }
    }

    // Bluff assassinate — aggressive + bluff-tolerant bots, but only when desperate or late-game
    if (!myCards.includes(CardType.Assassin) && this.p.aggression > 0.7 && this.p.bluffTolerance > 0.6) {
      const aliveOpponents = countAliveOpponents(obs, this.seat, this.numPlayers);
      const assTargets = actions.filter(a => a >= Action.AssassinateP0 && a <= Action.AssassinateP5);
      if (assTargets.length > 0 && aliveOpponents <= 2 && this.shouldBluff(CardType.Assassin, myCards, obs)) {
        return this.pickBestTarget(assTargets, "weakest", obs);
      }
    }

    // Tax — claim Duke even without it (bluff) if favorable
    if (actions.includes(Action.Tax)) {
      if (myCards.includes(CardType.Duke) || this.shouldBluff(CardType.Duke, myCards, obs)) {
        return Action.Tax;
      }
    }

    // Steal — claim Captain
    const stealTargets = actions.filter(a => a >= Action.StealP0 && a <= Action.StealP5);
    if (stealTargets.length > 0) {
      if (myCards.includes(CardType.Captain) || this.shouldBluff(CardType.Captain, myCards, obs)) {
        return this.pickBestTarget(stealTargets, "strongest", obs);
      }
    }

    // Exchange if holding Ambassador
    if (myCards.includes(CardType.Ambassador) && actions.includes(Action.Exchange)) {
      return Action.Exchange;
    }

    // Always claim Tax over Income — unless all Dukes are revealed (guaranteed challenge loss)
    if (actions.includes(Action.Tax) && this.revealedCounts[CardType.Duke] < CARD_COUNT) {
      return Action.Tax;
    }

    // Foreign Aid only when all Dukes are revealed (no one can block)
    if (actions.includes(Action.ForeignAid) && this.revealedCounts[CardType.Duke] >= CARD_COUNT) {
      return Action.ForeignAid;
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

    // Counter-bluff: block assassination with Contessa even without one
    if (blockActions.includes(Action.BlockContessa) && !myCards.includes(CardType.Contessa)) {
      const aliveCards = myCards.length;
      const contessaRevealed = this.revealedCounts[CardType.Contessa];

      const turnPlayer = getTurnPlayer(obs);
      const assassinClaimSuspicion = this.getPlayerSuspicion(turnPlayer, CardType.Assassin);

      // Always bluff-block if it would be fatal (1 influence left)
      if (aliveCards <= 1) {
        return Action.BlockContessa;
      }
      // bluffTolerance scales willingness to counter-bluff
      let bluffProb = 0.15 + this.p.bluffTolerance * 0.4; // 0.15 – 0.55
      if (contessaRevealed === 0) bluffProb += 0.1;
      if (assassinClaimSuspicion > 0.5) bluffProb += 0.1;
      if (Math.random() < bluffProb) {
        return Action.BlockContessa;
      }
    }

    // Bluff-block steal with Captain/Ambassador even without holding them
    for (const b of blockActions) {
      if (b === Action.BlockContessa) continue; // handled above
      const cardNeeded = blockToCard(b);
      if (cardNeeded !== null && !myCards.includes(cardNeeded)) {
        // Only high bluff tolerance bots bluff-block steals
        let prob = this.p.bluffTolerance * 0.35; // 0 – 0.35
        if (this.revealedCounts[cardNeeded] === 0) prob += 0.1;
        if (Math.random() < prob) {
          return b;
        }
      }
    }

    // Challenge based on the specific claimed card
    if (actions.includes(Action.Challenge)) {
      const pendingAction = getPendingAction(obs);
      const claimedCard = pendingAction !== null ? claimedRole(pendingAction) : null;
      const turnPlayer = getTurnPlayer(obs);

      // challengeRate scales all challenge probabilities
      const cr = 0.5 + this.p.challengeRate; // 0.5 – 1.5 multiplier

      if (claimedCard !== null) {
        const revealed = this.revealedCounts[claimedCard];

        // If all copies are revealed, challenge is guaranteed to succeed
        if (revealed >= CARD_COUNT) {
          return Action.Challenge;
        }

        // If 2 revealed, very likely bluffing
        if (revealed >= 2) {
          if (Math.random() < 0.8 * cr) return Action.Challenge;
        }

        // Check if this player has claimed too many different cards
        if (turnPlayer !== null) {
          const suspicion = this.getPlayerSuspicion(turnPlayer, claimedCard);
          if (suspicion > 0.7) {
            if (Math.random() < 0.6 * cr) return Action.Challenge;
          } else if (suspicion > 0.4) {
            if (Math.random() < 0.3 * cr) return Action.Challenge;
          }
        }

        // If 1 revealed, moderate challenge chance
        if (revealed === 1 && Math.random() < 0.2 * cr) {
          return Action.Challenge;
        }
      }

      // Baseline random challenge — scaled by challengeRate
      if (Math.random() < 0.1 * cr) {
        return Action.Challenge;
      }
    }

    return Action.Pass;
  }

  /** Returns 0-1 suspicion score for a player claiming a specific card */
  private getPlayerSuspicion(player: number | null, claimedCard: CardType): number {
    if (player === null) return 0;
    const claims = this.claimHistory.get(player);
    if (!claims) return 0;

    const distinctClaims = claims.size;
    if (distinctClaims >= 4) return 0.9;
    if (distinctClaims >= 3) return 0.7;

    if (!claims.has(claimedCard) && distinctClaims >= 2) return 0.5;

    return 0.1;
  }

  private shouldBluff(cardType: CardType, myCards: CardType[], obs: Float32Array): boolean {
    const revealed = this.revealedCounts[cardType];
    if (revealed >= 2) return false;

    // Base probability scaled by bluffTolerance
    let prob: number;
    if (revealed === 0) prob = 0.2 + this.p.bluffTolerance * 0.4; // 0.2 – 0.6
    else prob = 0.1 + this.p.bluffTolerance * 0.2; // 0.1 – 0.3

    // Context: bluff more when desperate
    const myCoins = getPlayerCoins(obs, this.seat);
    const myInfluence = myCards.length;

    if (myCoins <= 1 && myInfluence <= 1) prob += 0.25;
    else if (myCoins <= 2) prob += 0.1;

    if (myCoins >= 6 && myInfluence >= 2) prob -= 0.15;

    const aliveOpponents = countAliveOpponents(obs, this.seat, this.numPlayers);
    if (aliveOpponents <= 1 && myInfluence >= 2) prob -= 0.1;

    return Math.random() < Math.max(0, Math.min(1, prob));
  }

  private chooseLoseCard(actions: number[], myCards: CardType[], obs: Float32Array): number {
    const value = this.getCardValues(myCards, obs);

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

  /** Context-aware card valuation */
  private getCardValues(myCards: CardType[], obs: Float32Array): Record<CardType, number> {
    const base: Record<CardType, number> = {
      [CardType.Duke]: 5,
      [CardType.Captain]: 4,
      [CardType.Assassin]: 3,
      [CardType.Ambassador]: 2,
      [CardType.Contessa]: 1,
    };

    let anyCanAssassinate = false;
    let maxOpponentCoins = 0;
    let allOpponentsBroke = true;

    for (let p = 0; p < this.numPlayers; p++) {
      if (p === this.seat) continue;
      if (obs[OBS_ALIVE_BASE + p] < 0.5) continue;
      const coins = getPlayerCoins(obs, p);
      if (coins >= 3) anyCanAssassinate = true;
      if (coins > maxOpponentCoins) maxOpponentCoins = coins;
      if (coins >= 2) allOpponentsBroke = false;
    }

    if (anyCanAssassinate) {
      base[CardType.Contessa] += 3;
    }

    if (allOpponentsBroke) {
      base[CardType.Captain] -= 2;
    } else if (maxOpponentCoins >= 5) {
      base[CardType.Captain] += 1;
    }

    const myCoins = getPlayerCoins(obs, this.seat);
    if (myCoins < 2) {
      base[CardType.Assassin] -= 1;
    }

    const aliveOpponents = countAliveOpponents(obs, this.seat, this.numPlayers);
    if (aliveOpponents >= 3) {
      base[CardType.Ambassador] += 1;
    }

    // Aggressive bots value Assassin higher
    base[CardType.Assassin] += Math.round(this.p.aggression * 2 - 1); // -1 to +1

    // Greedy bots value Duke higher (more income generation)
    base[CardType.Duke] += Math.round(this.p.greed * 2 - 1); // -1 to +1

    return base;
  }

  private pickBestTarget(
    targetActions: number[],
    strategy: "strongest" | "weakest",
    obs: Float32Array,
  ): number {
    // Check for spite target first
    if (this.lastAttacker !== null && this.p.spite > 0.3) {
      const spiteAction = targetActions.find(a => {
        const tp = actionToTarget(a);
        return tp === this.lastAttacker;
      });
      // Spite probability: higher spite = more likely to retaliate
      if (spiteAction !== undefined && Math.random() < this.p.spite) {
        return spiteAction;
      }
    }

    let bestAction = targetActions[0];
    let bestScore = strategy === "strongest" ? -1 : Infinity;

    for (const a of targetActions) {
      const targetPlayer = actionToTarget(a);
      if (targetPlayer === null) continue;

      const coins = getPlayerCoins(obs, targetPlayer);
      const influence = getPlayerInfluence(obs, targetPlayer);

      if (strategy === "strongest") {
        const score = coins * 10 + influence;
        if (score > bestScore) {
          bestScore = score;
          bestAction = a;
        }
      } else {
        const score = influence * 100 + coins;
        if (score < bestScore) {
          bestScore = score;
          bestAction = a;
        }
      }
    }
    return bestAction;
  }
}

// ---- Obs tensor helpers ----

function actionToTarget(action: number): number | null {
  if (action >= Action.CoupP0 && action <= Action.CoupP5) return action - Action.CoupP0;
  if (action >= Action.StealP0 && action <= Action.StealP5) return action - Action.StealP0;
  if (action >= Action.AssassinateP0 && action <= Action.AssassinateP5) return action - Action.AssassinateP0;
  return null;
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

function getOwnCards(obs: Float32Array, seat: number): CardType[] {
  const base = seat * 12;
  const cards: CardType[] = [];
  if (obs[base + 5] > 0.5) {
    for (let i = 0; i < 5; i++) {
      if (obs[base + i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  if (obs[base + 11] > 0.5) {
    for (let i = 0; i < 5; i++) {
      if (obs[base + 6 + i] > 0.5) { cards.push(i as CardType); break; }
    }
  }
  return cards;
}

function getPlayerCoins(obs: Float32Array, player: number): number {
  return obs[OBS_COINS_BASE + player] * 12;
}

function getPlayerInfluence(obs: Float32Array, player: number): number {
  const base = player * 12;
  let count = 0;
  if (obs[base + 5] > 0.5) count++;
  if (obs[base + 11] > 0.5) count++;
  return count;
}

function countAliveOpponents(obs: Float32Array, seat: number, numPlayers: number): number {
  let count = 0;
  for (let p = 0; p < numPlayers; p++) {
    if (p !== seat && obs[OBS_ALIVE_BASE + p] > 0.5) count++;
  }
  return count;
}

function getPendingAction(obs: Float32Array): number | null {
  for (let i = 0; i < 32; i++) {
    if (obs[OBS_PENDING_BASE + i] > 0.5) return i;
  }
  return null;
}

function getTurnPlayer(obs: Float32Array): number | null {
  for (let i = 0; i < MAX_PLAYERS; i++) {
    if (obs[OBS_TURN_PLAYER_BASE + i] > 0.5) return i;
  }
  return null;
}
