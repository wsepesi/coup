// T12: Hard bot — probabilistic with card tracking, bluffs strategically

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
  private claimHistory: Map<number, Set<CardType>> = new Map();

  constructor(name: string, seat: number, numPlayers: number) {
    this.name = name;
    this.seat = seat;
    this.numPlayers = numPlayers;
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

    // Coup if can afford, target strongest
    const coupTargets = actions.filter(a => a >= Action.CoupP0 && a <= Action.CoupP5);
    if (coupTargets.length > 0) {
      const mustCoup = actions.length === coupTargets.length;
      if (mustCoup || myCoins >= 7) {
        return this.pickBestTarget(coupTargets, "strongest", obs);
      }
    }

    // Assassinate if can afford — target player with fewest influence (finish them off)
    if (myCards.includes(CardType.Assassin)) {
      const assTargets = actions.filter(a => a >= Action.AssassinateP0 && a <= Action.AssassinateP5);
      if (assTargets.length > 0) {
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

      // Get the assassinating player and check if they're likely bluffing
      const turnPlayer = getTurnPlayer(obs);
      const assassinClaimSuspicion = this.getPlayerSuspicion(turnPlayer, CardType.Assassin);

      // Always bluff-block if it would be fatal (1 influence left)
      if (aliveCards <= 1) {
        return Action.BlockContessa;
      }
      // More likely to bluff-block if fewer Contessas revealed (our bluff is more believable)
      // and if the assassin seems suspicious
      let bluffProb = 0.35;
      if (contessaRevealed === 0) bluffProb += 0.15;
      if (assassinClaimSuspicion > 0.5) bluffProb += 0.15; // they might be bluffing too
      if (Math.random() < bluffProb) {
        return Action.BlockContessa;
      }
    }

    // Challenge based on the specific claimed card
    if (actions.includes(Action.Challenge)) {
      const pendingAction = getPendingAction(obs);
      const claimedCard = pendingAction !== null ? claimedRole(pendingAction) : null;
      const turnPlayer = getTurnPlayer(obs);

      if (claimedCard !== null) {
        const revealed = this.revealedCounts[claimedCard];

        // If all copies are revealed, challenge is guaranteed to succeed
        if (revealed >= CARD_COUNT) {
          return Action.Challenge;
        }

        // If 2 revealed, very likely bluffing — high challenge rate
        if (revealed >= 2) {
          if (Math.random() < 0.8) return Action.Challenge;
        }

        // Check if this player has claimed too many different cards (likely bluffing)
        if (turnPlayer !== null) {
          const suspicion = this.getPlayerSuspicion(turnPlayer, claimedCard);
          if (suspicion > 0.7) {
            if (Math.random() < 0.6) return Action.Challenge;
          } else if (suspicion > 0.4) {
            if (Math.random() < 0.3) return Action.Challenge;
          }
        }

        // If 1 revealed, moderate challenge chance
        if (revealed === 1 && Math.random() < 0.2) {
          return Action.Challenge;
        }
      }

      // Low baseline random challenge
      if (Math.random() < 0.1) {
        return Action.Challenge;
      }
    }

    return Action.Pass;
  }

  /** Returns 0-1 suspicion score for a player claiming a specific card */
  private getPlayerSuspicion(player: number, claimedCard: CardType): number {
    const claims = this.claimHistory.get(player);
    if (!claims) return 0;

    // A player can hold at most 2 different card types
    // If they've claimed 3+ distinct types, at least one was a bluff
    const distinctClaims = claims.size;
    if (distinctClaims >= 4) return 0.9;
    if (distinctClaims >= 3) return 0.7;

    // If they haven't claimed this specific card before but have claimed 2 others,
    // they probably don't have it
    if (!claims.has(claimedCard) && distinctClaims >= 2) return 0.5;

    return 0.1;
  }

  private shouldBluff(cardType: CardType, myCards: CardType[], obs: Float32Array): boolean {
    const revealed = this.revealedCounts[cardType];
    // Never bluff if 2+ of the card are revealed (too easy to challenge)
    if (revealed >= 2) return false;

    // Base probability from revealed count
    let prob: number;
    if (revealed === 0) prob = 0.4;
    else prob = 0.2;

    // Context adjustment: bluff more aggressively when behind
    const myCoins = getPlayerCoins(obs, this.seat);
    const myInfluence = myCards.length;

    // Desperate: low coins and low influence → bluff more
    if (myCoins <= 1 && myInfluence <= 1) prob += 0.25;
    else if (myCoins <= 2) prob += 0.1;

    // Comfortable: high coins and full influence → bluff less
    if (myCoins >= 6 && myInfluence >= 2) prob -= 0.15;

    // If we're the last player with 2 influence, be more conservative
    const aliveOpponents = countAliveOpponents(obs, this.seat, this.numPlayers);
    if (aliveOpponents <= 1 && myInfluence >= 2) prob -= 0.1;

    return Math.random() < Math.max(0, Math.min(1, prob));
  }

  private chooseLoseCard(actions: number[], myCards: CardType[], obs: Float32Array): number {
    // Dynamic card valuation based on game state
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

    // Check if any opponent has enough coins to assassinate (3+)
    let anyCanAssassinate = false;
    // Check if opponents have high coins (steal is valuable)
    let maxOpponentCoins = 0;
    // Check if opponents are coin-starved (steal is less valuable)
    let allOpponentsBroke = true;

    for (let p = 0; p < this.numPlayers; p++) {
      if (p === this.seat) continue;
      if (obs[OBS_ALIVE_BASE + p] < 0.5) continue;
      const coins = getPlayerCoins(obs, p);
      if (coins >= 3) anyCanAssassinate = true;
      if (coins > maxOpponentCoins) maxOpponentCoins = coins;
      if (coins >= 2) allOpponentsBroke = false;
    }

    // Contessa is much more valuable if opponents can assassinate
    if (anyCanAssassinate) {
      base[CardType.Contessa] += 3;
    }

    // Captain is less valuable if all opponents are broke
    if (allOpponentsBroke) {
      base[CardType.Captain] -= 2;
    } else if (maxOpponentCoins >= 5) {
      // Captain is more valuable when there's a lot to steal
      base[CardType.Captain] += 1;
    }

    // Assassin is less valuable if we can't afford to assassinate (and aren't close)
    const myCoins = getPlayerCoins(obs, this.seat);
    if (myCoins < 2) {
      base[CardType.Assassin] -= 1;
    }

    // Ambassador is more valuable early (more unknown cards to swap)
    const aliveOpponents = countAliveOpponents(obs, this.seat, this.numPlayers);
    if (aliveOpponents >= 3) {
      base[CardType.Ambassador] += 1;
    }

    return base;
  }

  private pickBestTarget(
    targetActions: number[],
    strategy: "strongest" | "weakest",
    obs: Float32Array,
  ): number {
    let bestAction = targetActions[0];
    let bestScore = strategy === "strongest" ? -1 : Infinity;

    for (const a of targetActions) {
      let targetPlayer: number;
      if (a >= Action.CoupP0 && a <= Action.CoupP5) targetPlayer = a - Action.CoupP0;
      else if (a >= Action.StealP0 && a <= Action.StealP5) targetPlayer = a - Action.StealP0;
      else if (a >= Action.AssassinateP0 && a <= Action.AssassinateP5) targetPlayer = a - Action.AssassinateP0;
      else continue;

      const coins = getPlayerCoins(obs, targetPlayer);
      const influence = getPlayerInfluence(obs, targetPlayer);

      if (strategy === "strongest") {
        // Prefer highest coins, break ties by influence
        const score = coins * 10 + influence;
        if (score > bestScore) {
          bestScore = score;
          bestAction = a;
        }
      } else {
        // "weakest" — prefer fewest influence, break ties by fewest coins
        // (finish off players about to die)
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
  return obs[OBS_COINS_BASE + player] * 12; // denormalize
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
