// Heuristic bots for the Workers game server.
//
// Bots only ever see a BotView: public information (coins, influence, revealed
// cards, public claims) plus their own hand. They cannot read other players'
// hidden cards or the deck — the view is built by the room, not handed the
// raw engine.
//
//   easy   — random legal main action, never challenges or blocks.
//   medium — honest: only claims/blocks with roles it holds; challenges only
//            when the claim is provably (or very likely) a bluff.
//   hard   — bluffs, bluff-blocks, and challenges using card counting.

import {
  ACTION_INCOME,
  ACTION_FOREIGN_AID,
  ACTION_TAX,
  ACTION_EXCHANGE,
  ACTION_CHALLENGE,
  ACTION_PASS,
  ACTION_BLOCK_CONTESSA,
  ACTION_DISCARD_SLOT0,
  PHASE_CHALLENGE_ACTION,
  PHASE_CHALLENGE_BLOCK,
  PHASE_BLOCK,
  PHASE_EXCHANGE_DISCARD,
  DUKE,
  ASSASSIN,
  CAPTAIN,
  AMBASSADOR,
  CONTESSA,
  isCoup,
  isSteal,
  isAssassinate,
  isBlock,
  isDiscard,
  actionTarget,
  claimedRole,
  blockRole,
  type BotDifficulty,
} from "./types.js";

const COPIES = 3;
const DECK_SIZE = 15;

/** Everything a bot may legitimately know when deciding. */
export interface BotView {
  seat: number;
  numPlayers: number;
  phase: number;
  turnPlayer: number;
  /** Main action under resolution (meaningless during the action phase). */
  pending: number;
  /** Seat and role of the current block (only meaningful in challenge_block). */
  blocker: number;
  blockCard: number;
  actions: number[];
  /** Own card type per discard slot: 0/1 = hand, 2/3 = drawn (exchange only). null = dead/absent. */
  slots: (number | null)[];
  coins: number[];
  influence: number[];
  /** Count of each role revealed face-up anywhere at the table. */
  revealed: number[];
  /** Roles each seat has publicly claimed this game. */
  claims: Set<number>[];
}

const BOT_NAMES = [
  "Matt", "Lucia", "Elisa", "Tyrone", "Abby", "Ren", "Sakura", "Pierre",
  "Haru", "Marco", "Emily", "Takumi", "Miyu", "Oscar", "Silke", "Theo",
  "Naomi", "Kenji", "Gabi", "Luca", "Yuki", "Steph", "Akira", "Dina",
  "Tommy", "Mia", "Fritz", "Elena", "Ravi", "Anna",
];

/** A bot name not already used in the room. */
export function pickBotName(taken: Iterable<string>): string {
  const used = new Set(Array.from(taken, (n) => n.toLowerCase()));
  const free = BOT_NAMES.filter((n) => !used.has(n.toLowerCase()));
  if (free.length > 0) return free[Math.floor(Math.random() * free.length)];
  for (let i = 2; ; i++) {
    const n = `Bot ${i}`;
    if (!used.has(n.toLowerCase())) return n;
  }
}

export function chooseBotAction(difficulty: BotDifficulty, v: BotView): number {
  const actions = v.actions;
  if (actions.length === 0) return ACTION_PASS;
  if (actions.length === 1) return actions[0];

  if (actions.some(isDiscard)) return chooseDiscard(v, difficulty);

  const responding = actions.includes(ACTION_PASS);
  if (difficulty === "easy") {
    if (responding) return ACTION_PASS;
    return actions[Math.floor(Math.random() * actions.length)];
  }
  if (responding) return respond(v, difficulty === "hard");
  return difficulty === "hard" ? hardMain(v) : mediumMain(v);
}

// ---------------------------------------------------------------------------
// Helpers

function myCards(v: BotView): number[] {
  return [v.slots[0], v.slots[1]].filter((c): c is number => c != null);
}

function aliveOpponents(v: BotView): number[] {
  const out: number[] = [];
  for (let p = 0; p < v.numPlayers; p++) if (p !== v.seat && v.influence[p] > 0) out.push(p);
  return out;
}

/** Probability that `seat` holds at least one `role`, from public info + own hand. */
function probHolds(v: BotView, seat: number, role: number): number {
  const mine = myCards(v).filter((c) => c === role).length;
  const unseenCopies = COPIES - v.revealed[role] - mine;
  if (unseenCopies <= 0) return 0;
  // Cards whose identity is unknown to us: everything except revealed cards and our hand.
  const revealedTotal = v.revealed.reduce((a, b) => a + b, 0);
  const unknown = DECK_SIZE - revealedTotal - myCards(v).length;
  const k = v.influence[seat];
  if (k <= 0 || unknown <= 0) return 0;
  // P(none of k hidden cards is the role) = C(unknown - copies, k) / C(unknown, k)
  let pNone = 1;
  for (let i = 0; i < k; i++) pNone *= Math.max(0, unknown - unseenCopies - i) / (unknown - i);
  return 1 - pNone;
}

function targetsOf(actions: number[], pred: (a: number) => boolean): number[] {
  return actions.filter(pred);
}

/** Threat score for picking targets: coins and influence, with a nudge toward leaders. */
function threat(v: BotView, p: number): number {
  return v.coins[p] * 10 + v.influence[p] * 25;
}

function pickBy(actions: number[], score: (target: number) => number): number {
  let best = actions[0];
  let bestScore = -Infinity;
  for (const a of actions) {
    const t = actionTarget(a)!;
    const s = score(t) + Math.random(); // random tie-break
    if (s > bestScore) { bestScore = s; best = a; }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Main actions

function mediumMain(v: BotView): number {
  const a = v.actions;
  const cards = myCards(v);
  const coins = v.coins[v.seat];
  const coups = targetsOf(a, isCoup);
  if (coups.length > 0 && coins >= 7) return pickBy(coups, (t) => threat(v, t));

  if (cards.includes(ASSASSIN) && coins >= 3) {
    const ts = targetsOf(a, isAssassinate);
    if (ts.length) return pickBy(ts, (t) => -v.influence[t] * 100 + threat(v, t) - (v.claims[t].has(CONTESSA) ? 60 : 0));
  }
  if (cards.includes(DUKE) && a.includes(ACTION_TAX)) return ACTION_TAX;
  if (cards.includes(CAPTAIN)) {
    const ts = targetsOf(a, isSteal).filter((x) => v.coins[actionTarget(x)!] > 0);
    if (ts.length) return pickBy(ts, (t) => Math.min(2, v.coins[t]) * 100 - (v.claims[t].has(CAPTAIN) || v.claims[t].has(AMBASSADOR) ? 150 : 0));
  }
  if (cards.includes(AMBASSADOR) && a.includes(ACTION_EXCHANGE)) return ACTION_EXCHANGE;
  // Foreign aid is only blockable by Duke; take it unless someone has been claiming Duke.
  const dukeClaimed = aliveOpponents(v).some((p) => v.claims[p].has(DUKE));
  if (a.includes(ACTION_FOREIGN_AID) && !dukeClaimed) return ACTION_FOREIGN_AID;
  return a.includes(ACTION_INCOME) ? ACTION_INCOME : a[0];
}

function hardMain(v: BotView): number {
  const a = v.actions;
  const cards = myCards(v);
  const coins = v.coins[v.seat];
  const opps = aliveOpponents(v);
  const coups = targetsOf(a, isCoup);
  if (coups.length > 0 && (coins >= 7 && (coins >= 8 || opps.length <= 2) || a.every(isCoup))) {
    return pickBy(coups, (t) => threat(v, t));
  }

  const has = (r: number) => cards.includes(r);
  const exposed = (r: number) => v.revealed[r] + cards.filter((c) => c === r).length;
  // Willingness to bluff a role falls as more copies are visible and as we have less to lose.
  const bluff = (r: number) => {
    if (exposed(r) >= 2) return false;
    let p = exposed(r) === 0 ? 0.45 : 0.2;
    if (cards.length === 1) p -= 0.15;
    if (v.claims[v.seat].has(r)) p += 0.3; // stay consistent with earlier claims
    return Math.random() < p;
  };

  if (coins >= 3 && (has(ASSASSIN) || (coins < 7 && bluff(ASSASSIN)))) {
    const ts = targetsOf(a, isAssassinate);
    if (ts.length) return pickBy(ts, (t) => -v.influence[t] * 100 + threat(v, t) - (v.claims[t].has(CONTESSA) ? 80 : 0));
  }
  if (a.includes(ACTION_TAX) && (has(DUKE) || bluff(DUKE))) return ACTION_TAX;
  const steals = targetsOf(a, isSteal).filter((x) => v.coins[actionTarget(x)!] >= 2);
  if (steals.length && (has(CAPTAIN) || bluff(CAPTAIN))) {
    return pickBy(steals, (t) => v.coins[t] * 10 - (v.claims[t].has(CAPTAIN) || v.claims[t].has(AMBASSADOR) ? 60 : 0));
  }
  if (a.includes(ACTION_EXCHANGE) && has(AMBASSADOR) && !has(DUKE)) return ACTION_EXCHANGE;
  const dukeClaimed = opps.some((p) => v.claims[p].has(DUKE));
  if (a.includes(ACTION_FOREIGN_AID) && !dukeClaimed) return ACTION_FOREIGN_AID;
  if (a.includes(ACTION_TAX) && exposed(DUKE) < COPIES && Math.random() < 0.5) return ACTION_TAX;
  return a.includes(ACTION_INCOME) ? ACTION_INCOME : a[0];
}

// ---------------------------------------------------------------------------
// Responses: challenge / block / pass

function respond(v: BotView, hard: boolean): number {
  const a = v.actions;
  const cards = myCards(v);
  const lives = cards.length;

  if (v.phase === PHASE_BLOCK) {
    const blocks = a.filter(isBlock);
    // Honest block when holding the card.
    for (const b of blocks) if (cards.includes(blockRole(b)!)) return b;

    const pa = v.pending;
    const targetedAtMe = actionTarget(pa) === v.seat;
    if (isAssassinate(pa) && targetedAtMe) {
      // Facing death with one card: bluff Contessa (both levels — passing loses anyway).
      if (lives === 1 && a.includes(ACTION_BLOCK_CONTESSA) && v.revealed[CONTESSA] < COPIES) return ACTION_BLOCK_CONTESSA;
      if (hard && a.includes(ACTION_BLOCK_CONTESSA) && v.revealed[CONTESSA] < 2 && Math.random() < 0.35) return ACTION_BLOCK_CONTESSA;
    }
    if (hard && isSteal(pa) && targetedAtMe && v.coins[v.seat] >= 2) {
      for (const b of blocks) {
        const r = blockRole(b)!;
        if (v.revealed[r] < 2 && Math.random() < 0.25) return b;
      }
    }
    return ACTION_PASS;
  }

  if (!a.includes(ACTION_CHALLENGE)) return ACTION_PASS;

  let claimant: number;
  let role: number | null;
  if (v.phase === PHASE_CHALLENGE_ACTION) {
    claimant = v.turnPlayer;
    role = claimedRole(v.pending);
  } else if (v.phase === PHASE_CHALLENGE_BLOCK) {
    claimant = v.blocker;
    role = v.blockCard;
  } else {
    return ACTION_PASS;
  }
  if (role == null || role < 0 || role > 4) return ACTION_PASS;

  const pHas = probHolds(v, claimant, role);
  if (pHas === 0) return ACTION_CHALLENGE; // provably a bluff

  // Stakes: an assassination aimed at us with one life left means passing is fatal.
  const mortal = v.phase === PHASE_CHALLENGE_ACTION && isAssassinate(v.pending) && actionTarget(v.pending) === v.seat;
  if (mortal && lives === 1 && !cards.includes(CONTESSA)) {
    // Medium will also take this gamble — it costs nothing.
    if (!hard || pHas < 0.85 || Math.random() < 0.5) return ACTION_CHALLENGE;
  }
  // Our own block being challenged-back isn't a decision here; blocked actor deciding whether
  // to contest a block on their own action is the most valuable challenge.
  const myActionBlocked = v.phase === PHASE_CHALLENGE_BLOCK && v.turnPlayer === v.seat;

  if (!hard) {
    return pHas < 0.1 && lives === 2 ? ACTION_CHALLENGE : ACTION_PASS;
  }

  let threshold = lives === 2 ? 0.3 : 0.15;
  if (myActionBlocked) threshold += 0.1;
  // A role claimed many times by different players is more suspicious.
  const otherClaimers = v.claims.filter((s, p) => p !== claimant && v.influence[p] > 0 && s.has(role!)).length;
  const adjusted = pHas - 0.1 * otherClaimers;
  if (adjusted < threshold) return Math.random() < 0.75 ? ACTION_CHALLENGE : ACTION_PASS;
  // Rare random challenge to stay unpredictable, spread over the table.
  if (lives === 2 && Math.random() < 0.04) return ACTION_CHALLENGE;
  return ACTION_PASS;
}

// ---------------------------------------------------------------------------
// Discards: losing influence, or picking cards to return during an exchange.

function cardValues(v: BotView, hard: boolean): number[] {
  const val = [5, 3, 4, 2, 1]; // Duke, Assassin, Captain, Ambassador, Contessa
  if (!hard) return val;
  const opps = aliveOpponents(v);
  if (opps.some((p) => v.coins[p] >= 3)) val[CONTESSA] += 3;
  if (opps.every((p) => v.coins[p] < 2)) val[CAPTAIN] -= 2;
  if (v.coins[v.seat] < 2) val[ASSASSIN] -= 1;
  if (opps.length >= 3) val[AMBASSADOR] += 1;
  return val;
}

function chooseDiscard(v: BotView, difficulty: BotDifficulty): number {
  const discards = v.actions.filter(isDiscard);
  if (difficulty === "easy" && v.phase !== PHASE_EXCHANGE_DISCARD) {
    return discards[Math.floor(Math.random() * discards.length)];
  }
  const val = cardValues(v, difficulty === "hard");

  if (v.phase === PHASE_EXCHANGE_DISCARD) {
    // Deterministically choose the best cards to keep; the engine takes the two
    // discards as two ordered steps (and only applies them after the second), so
    // recomputing the same plan on step two yields the matching second slot.
    const avail: number[] = [];
    for (let s = 0; s < 4; s++) if (v.slots[s] != null) avail.push(s);
    const keepN = avail.length - 2;
    let best: number[] = [];
    let bestScore = -Infinity;
    const combos = keepN === 1 ? avail.map((s) => [s]) : pairs(avail);
    for (const keep of combos) {
      const types = keep.map((s) => v.slots[s]!);
      let score = types.reduce((acc, t) => acc + val[t], 0);
      if (types.length === 2 && types[0] === types[1]) score -= 2; // prefer variety
      if (score > bestScore) { bestScore = score; best = keep; }
    }
    const drop = avail.filter((s) => !best.includes(s)).sort((x, y) => x - y);
    for (const s of drop) {
      const act = ACTION_DISCARD_SLOT0 + s;
      if (discards.includes(act)) return act;
    }
    return discards[0];
  }

  let best = discards[0];
  let bestVal = Infinity;
  for (const a of discards) {
    const t = v.slots[a - ACTION_DISCARD_SLOT0];
    const value = t == null ? -1 : val[t];
    if (value < bestVal) { bestVal = value; best = a; }
  }
  return best;
}

function pairs(xs: number[]): number[][] {
  const out: number[][] = [];
  for (let i = 0; i < xs.length; i++) for (let j = i + 1; j < xs.length; j++) out.push([xs[i], xs[j]]);
  return out;
}

