// Thin bridge to the C engine compiled as a standalone WASM module
// (wasm/build.sh). Export names are unminified, so we look them up by name.
//
// Memory model: one WASM instance per isolate, shared by every GameRoom that
// lives in it. Instead of malloc'ing a Game per room (which leaks whenever a
// Durable Object is evicted without cleanup and eventually exhausts the fixed
// heap), each CoupGame owns its Game bytes in JS and copies them into a single
// static scratch struct before calling into C. The bytes are what gets
// persisted to Durable Object storage, so a room survives eviction/hibernation.

// @ts-ignore — wrangler bundles .wasm imports as WebAssembly.Module
import COUP_WASM from "../../wasm/coup.wasm";

interface Exports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  game_init(g: number, numPlayers: number, dealSeed: bigint, procSeed: bigint): void;
  /** 0 = applied, -1 = invalid (state untouched). */
  step_with_rng(g: number, action: number): number;
  get_valid_actions(g: number): number;
  is_done(g: number): number;
  get_winner(g: number): number;
  is_chance_node(g: number): number;
  wasm_scratch(): number;
  wasm_game_struct_size(): number;
  wasm_get_phase(g: number): number;
  wasm_get_turn_player(g: number): number;
  wasm_get_active_player(g: number): number;
  wasm_get_pending_action(g: number): number;
  wasm_get_num_players(g: number): number;
  wasm_player_card0_type(g: number, p: number): number;
  wasm_player_card0_alive(g: number, p: number): number;
  wasm_player_card1_type(g: number, p: number): number;
  wasm_player_card1_alive(g: number, p: number): number;
  wasm_player_coins(g: number, p: number): number;
  wasm_player_is_alive(g: number, p: number): number;
  wasm_deck_total(g: number): number;
  wasm_get_exchange_card0(g: number): number;
  wasm_get_exchange_card1(g: number): number;
  wasm_get_blocker(g: number): number;
  wasm_get_block_card(g: number): number;
  wasm_set_refund_on_challenge(g: number, flag: number): void;
}

let exp: Exports | null = null;
let scratchPtr = 0;
let gameSize = 0;
let owner: CoupGame | null = null; // whose bytes are currently in scratch

export async function loadEngine(): Promise<void> {
  if (exp) return;
  // Supply a stub for any import the module might declare (currently none);
  // this keeps us working if a future engine change pulls in a libc import.
  const imports: Record<string, Record<string, unknown>> = {};
  for (const imp of WebAssembly.Module.imports(COUP_WASM as WebAssembly.Module)) {
    (imports[imp.module] ??= {})[imp.name] =
      imp.kind === "function" ? () => 0 : undefined;
  }
  const instance = await WebAssembly.instantiate(COUP_WASM as WebAssembly.Module, imports as WebAssembly.Imports);
  const e = instance.exports as unknown as Exports;
  e._initialize?.();
  scratchPtr = e.wasm_scratch();
  gameSize = e.wasm_game_struct_size();
  exp = e;
}

function engine(): Exports {
  if (!exp) throw new Error("engine not loaded");
  return exp;
}

function randomSeed(): bigint {
  const a = new BigUint64Array(1);
  crypto.getRandomValues(a);
  return a[0] || 1n;
}

export class CoupGame {
  readonly bytes: Uint8Array;

  private constructor(bytes: Uint8Array) {
    this.bytes = bytes;
  }

  /** New game with a fresh crypto-random deal. Requires loadEngine(). */
  static create(numPlayers: number, refundOnChallenge: boolean): CoupGame {
    const e = engine();
    e.game_init(scratchPtr, numPlayers, randomSeed(), randomSeed());
    e.wasm_set_refund_on_challenge(scratchPtr, refundOnChallenge ? 1 : 0);
    const g = new CoupGame(new Uint8Array(gameSize));
    g.save();
    owner = g;
    return g;
  }

  /** Rehydrate from persisted bytes. Requires loadEngine(). */
  static restore(bytes: Uint8Array): CoupGame {
    if (bytes.length !== gameSize) {
      throw new Error(`Game state size mismatch: stored ${bytes.length}, engine ${gameSize}`);
    }
    return new CoupGame(new Uint8Array(bytes));
  }

  private ptr(): number {
    if (owner !== this) {
      new Uint8Array(engine().memory.buffer, scratchPtr, gameSize).set(this.bytes);
      owner = this;
    }
    return scratchPtr;
  }

  private save(): void {
    this.bytes.set(new Uint8Array(engine().memory.buffer, scratchPtr, gameSize));
  }

  /** Apply an action; false if the engine rejected it (state untouched). */
  step(action: number): boolean {
    const rc = engine().step_with_rng(this.ptr(), action);
    if (rc !== 0) return false;
    this.save();
    return true;
  }

  validMask(): number { return engine().get_valid_actions(this.ptr()) >>> 0; }
  isValid(action: number): boolean {
    return Number.isInteger(action) && action >= 0 && action < 32 && ((this.validMask() >>> action) & 1) === 1;
  }
  validActions(): number[] {
    const m = this.validMask();
    const out: number[] = [];
    for (let i = 0; i < 32; i++) if ((m >>> i) & 1) out.push(i);
    return out;
  }
  isDone(): boolean { return engine().is_done(this.ptr()) !== 0; }
  winner(): number { return engine().get_winner(this.ptr()); }
  isChanceNode(): boolean { return engine().is_chance_node(this.ptr()) !== 0; }

  phase(): number { return engine().wasm_get_phase(this.ptr()); }
  turnPlayer(): number { return engine().wasm_get_turn_player(this.ptr()); }
  activePlayer(): number { return engine().wasm_get_active_player(this.ptr()); }
  pendingAction(): number { return engine().wasm_get_pending_action(this.ptr()); }
  numPlayers(): number { return engine().wasm_get_num_players(this.ptr()); }
  cardType(p: number, slot: 0 | 1): number {
    return slot === 0 ? engine().wasm_player_card0_type(this.ptr(), p) : engine().wasm_player_card1_type(this.ptr(), p);
  }
  cardAlive(p: number, slot: 0 | 1): boolean {
    return (slot === 0 ? engine().wasm_player_card0_alive(this.ptr(), p) : engine().wasm_player_card1_alive(this.ptr(), p)) !== 0;
  }
  coins(p: number): number { return engine().wasm_player_coins(this.ptr(), p); }
  isAlive(p: number): boolean { return engine().wasm_player_is_alive(this.ptr(), p) !== 0; }
  influence(p: number): number { return (this.cardAlive(p, 0) ? 1 : 0) + (this.cardAlive(p, 1) ? 1 : 0); }
  deckTotal(): number { return engine().wasm_deck_total(this.ptr()); }
  exchangeCards(): [number, number] {
    const e = engine();
    const p = this.ptr();
    return [e.wasm_get_exchange_card0(p), e.wasm_get_exchange_card1(p)];
  }
  blocker(): number { return engine().wasm_get_blocker(this.ptr()); }
  blockCard(): number { return engine().wasm_get_block_card(this.ptr()); }
}
