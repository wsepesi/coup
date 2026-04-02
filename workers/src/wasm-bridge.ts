// Direct WASM bridge — bypasses Emscripten JS glue entirely.
// The WASM has one import (a.a = emscripten_resize_heap) which is never called
// since we compile with ALLOW_MEMORY_GROWTH=0.

// @ts-ignore — WASM module loaded via wrangler bundler
import COUP_WASM from "../../wasm/coup.wasm";

// The Emscripten compiler minifies export names. We map them here.
// These come from the assignWasmExports function in the Emscripten glue (coup.js).
const EXPORT_MAP = {
  memory: "b",
  __wasm_call_ctors: "c",
  game_init: "d",
  apply_chance: "e",
  is_chance_node: "f",
  chance_outcomes: "g",
  step_deterministic: "h",
  step_with_rng: "i",
  is_done: "j",
  get_valid_actions: "k",
  get_active_player_ext: "l",
  get_winner: "m",
  get_num_players_ext: "n",
  get_deck_total: "o",
  observe: "p",
  gamelog_init: "q",
  wasm_get_phase: "C",
  wasm_get_turn_player: "D",
  wasm_get_active_player: "E",
  wasm_get_pending_action: "F",
  wasm_get_num_players: "G",
  wasm_player_card0_type: "H",
  wasm_player_card0_alive: "I",
  wasm_player_card1_type: "J",
  wasm_player_card1_alive: "K",
  wasm_player_coins: "L",
  wasm_player_is_alive: "M",
  wasm_deck_total: "N",
  wasm_get_exchange_card0: "O",
  wasm_get_exchange_card1: "P",
  wasm_game_struct_size: "T",
  wasm_gamelog_struct_size: "U",
  wasm_history_struct_size: "V",
  wasm_chanceoutcome_struct_size: "X",
  malloc: "Y",
  free: "Z",
} as const;

interface WasmExports {
  memory: WebAssembly.Memory;
  [key: string]: any;
}

let cachedExports: WasmExports | null = null;

async function loadWasm(): Promise<WasmExports> {
  if (cachedExports) return cachedExports;

  const instance = await WebAssembly.instantiate(COUP_WASM, {
    a: {
      a: () => { /* emscripten_resize_heap — never called with ALLOW_MEMORY_GROWTH=0 */ return 0; },
    },
  });

  const raw = instance.exports as any;

  // Call __wasm_call_ctors to initialize
  if (typeof raw[EXPORT_MAP.__wasm_call_ctors] === "function") {
    raw[EXPORT_MAP.__wasm_call_ctors]();
  }

  // Build a friendly exports object
  const exp: any = {};
  for (const [name, minified] of Object.entries(EXPORT_MAP)) {
    exp[name] = raw[minified];
  }

  cachedExports = exp as WasmExports;
  return cachedExports;
}

export interface ChanceOutcome {
  outcome: number;
  probability: number;
}

export class CoupWasm {
  private exp: WasmExports;
  private gamePtr: number;
  private historyPtr: number;
  private obsPtr: number;
  private chancePtr: number;
  private disposed = false;

  private constructor(exp: WasmExports, numPlayers: number) {
    this.exp = exp;

    const gameSize = exp.wasm_game_struct_size();
    this.gamePtr = exp.malloc(gameSize);

    const histSize = exp.wasm_history_struct_size();
    this.historyPtr = exp.malloc(histSize);
    new Uint8Array(exp.memory.buffer).fill(0, this.historyPtr, this.historyPtr + histSize);

    this.obsPtr = exp.malloc(407 * 4);
    this.chancePtr = exp.malloc(5 * 16);

    const seed1 = BigInt(Math.floor(Math.random() * Number.MAX_SAFE_INTEGER));
    const seed2 = BigInt(Math.floor(Math.random() * Number.MAX_SAFE_INTEGER));
    exp.game_init(this.gamePtr, numPlayers, seed1, seed2);
  }

  static async create(numPlayers: number): Promise<CoupWasm> {
    const exp = await loadWasm();
    return new CoupWasm(exp, numPlayers);
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.exp.free(this.gamePtr);
    this.exp.free(this.historyPtr);
    this.exp.free(this.obsPtr);
    this.exp.free(this.chancePtr);
  }

  stepDeterministic(action: number): void { this.exp.step_deterministic(this.gamePtr, action); }
  stepWithRng(action: number): void { this.exp.step_with_rng(this.gamePtr, action); }
  getValidActions(): number { return this.exp.get_valid_actions(this.gamePtr); }
  isDone(): boolean { return this.exp.is_done(this.gamePtr) !== 0; }
  getWinner(): number { return this.exp.get_winner(this.gamePtr); }
  isChanceNode(): boolean { return this.exp.is_chance_node(this.gamePtr) !== 0; }

  chanceOutcomes(): ChanceOutcome[] {
    const count = this.exp.chance_outcomes(this.gamePtr, this.chancePtr);
    const results: ChanceOutcome[] = [];
    const dv = new DataView(this.exp.memory.buffer);
    for (let i = 0; i < count; i++) {
      const base = this.chancePtr + i * 16;
      results.push({
        outcome: dv.getInt32(base, true),
        probability: dv.getFloat64(base + 8, true),
      });
    }
    return results;
  }

  applyChance(outcome: number): void { this.exp.apply_chance(this.gamePtr, outcome); }

  resolveChance(): void {
    const outcomes = this.chanceOutcomes();
    if (outcomes.length === 0) return;
    const r = Math.random();
    let cum = 0;
    for (const o of outcomes) {
      cum += o.probability;
      if (r < cum) { this.applyChance(o.outcome); return; }
    }
    this.applyChance(outcomes[outcomes.length - 1].outcome);
  }

  getPhase(): number { return this.exp.wasm_get_phase(this.gamePtr); }
  getTurnPlayer(): number { return this.exp.wasm_get_turn_player(this.gamePtr); }
  getActivePlayer(): number { return this.exp.wasm_get_active_player(this.gamePtr); }
  getPendingAction(): number { return this.exp.wasm_get_pending_action(this.gamePtr); }
  getNumPlayers(): number { return this.exp.wasm_get_num_players(this.gamePtr); }
  playerCard0Type(p: number): number { return this.exp.wasm_player_card0_type(this.gamePtr, p); }
  playerCard0Alive(p: number): boolean { return this.exp.wasm_player_card0_alive(this.gamePtr, p) !== 0; }
  playerCard1Type(p: number): number { return this.exp.wasm_player_card1_type(this.gamePtr, p); }
  playerCard1Alive(p: number): boolean { return this.exp.wasm_player_card1_alive(this.gamePtr, p) !== 0; }
  playerCoins(p: number): number { return this.exp.wasm_player_coins(this.gamePtr, p); }
  playerIsAlive(p: number): boolean { return this.exp.wasm_player_is_alive(this.gamePtr, p) !== 0; }
  deckTotal(): number { return this.exp.wasm_deck_total(this.gamePtr); }
  exchangeCard0(): number { return this.exp.wasm_get_exchange_card0(this.gamePtr); }
  exchangeCard1(): number { return this.exp.wasm_get_exchange_card1(this.gamePtr); }

  observe(playerId: number): Float32Array {
    this.exp.observe(this.gamePtr, playerId, this.historyPtr, this.obsPtr);
    const floats = new Float32Array(this.exp.memory.buffer, this.obsPtr, 407);
    return new Float32Array(floats); // copy
  }
}
