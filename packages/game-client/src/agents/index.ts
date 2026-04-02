export { EasyBot } from "./heuristic-easy.js";
export { MediumBot } from "./heuristic-medium.js";
export { HardBot, randomProfile } from "./heuristic-hard.js";
export type { CharacterProfile } from "./heuristic-hard.js";
export { OnnxBot } from "./onnx-stub.js";

import { EasyBot } from "./heuristic-easy.js";
import { MediumBot } from "./heuristic-medium.js";
import { HardBot, randomProfile } from "./heuristic-hard.js";

const MII_NAMES = [
  "Matt", "Lucia", "Elisa", "Tyrone", "Abby", "Ren", "Sakura", "Pierre",
  "Haru", "Marco", "Emily", "Takumi", "Miyu", "Oscar", "Silke", "Theo",
  "Naomi", "Kenji", "Gabi", "Luca", "Yuki", "Steph", "Akira", "Dina",
  "Tommy", "Mia", "Fritz", "Elena", "Ravi", "Anna",
];

// ── SplitMix64 PRNG (matches c_engine/prng.h) ──

const MASK64 = 0xFFFFFFFFFFFFFFFFn;

function splitmix64(state: { s: bigint }): bigint {
  let z = (state.s = (state.s + 0x9E3779B97F4A7C15n) & MASK64);
  z = ((z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n) & MASK64;
  z = ((z ^ (z >> 27n)) * 0x94D049BB133111EBn) & MASK64;
  return (z ^ (z >> 31n)) & MASK64;
}

/** Returns a () => number function producing values in [0, 1) from a seeded PRNG */
function makeRng(seed: bigint): () => number {
  const state = { s: seed };
  return () => {
    const v = splitmix64(state);
    // Use upper 32 bits for quality, divide by 2^32
    return Number(v >> 32n) / 0x100000000;
  };
}

let namePool: string[] = [];

function shuffleNames(rng: () => number) {
  namePool = [...MII_NAMES];
  for (let i = namePool.length - 1; i > 0; i--) {
    const j = Math.floor(rng() * (i + 1));
    [namePool[i], namePool[j]] = [namePool[j], namePool[i]];
  }
}

/**
 * Initialize bot name pool and PRNG from the game seed.
 * Must be called before createBot. Uses a derived seed (seed + 1000)
 * so it doesn't collide with the C engine's deal/proc seeds.
 */
export function initBots(seed: bigint) {
  const rng = makeRng(seed + 1000n);
  shuffleNames(rng);
  _botRng = rng;
}

let _botRng: (() => number) | null = null;

export function createBot(difficulty: "easy" | "medium" | "hard" | "hard+", seat: number, numPlayers: number) {
  const rng = _botRng ?? Math.random;
  if (namePool.length === 0) {
    // Fallback if initBots wasn't called (e.g. training)
    shuffleNames(rng);
  }
  const name = namePool.pop()!;
  switch (difficulty) {
    case "easy": return new EasyBot(name);
    case "medium": return new MediumBot(name, seat);
    case "hard": return new HardBot(name, seat, numPlayers);
    case "hard+": return new HardBot(name, seat, numPlayers, randomProfile(rng));
  }
}

export function resetBotCounter() {
  // Legacy compat — prefer initBots(seed) instead
  namePool = [];
  _botRng = null;
}
