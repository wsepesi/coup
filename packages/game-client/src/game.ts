// T9: CoupGame wrapper — typed TS API over FFI calls

import { getLib, allocGameBuffer, ptr, GAME_STRUCT_SIZE } from "./ffi.js";
import { Phase, CardType, OBS_SIZE, type GameSnapshot, type PlayerState, type CardState } from "./types.js";

export class CoupGame {
  private gameBuffer: ArrayBuffer;
  private gamePtr: ReturnType<typeof ptr>;
  private obsBuffer: Float32Array;
  private obsPtr: ReturnType<typeof ptr>;
  private playerView: Uint16Array;
  private _numPlayers: number;

  constructor(numPlayers: number, seed?: bigint) {
    const lib = getLib();
    this.gameBuffer = allocGameBuffer();
    this.gamePtr = ptr(this.gameBuffer);
    this.obsBuffer = new Float32Array(OBS_SIZE);
    this.obsPtr = ptr(this.obsBuffer.buffer);
    this.playerView = new Uint16Array(this.gameBuffer, 0, 6);
    this._numPlayers = numPlayers;

    const dealSeed = seed ?? BigInt(Math.floor(Math.random() * 2 ** 32));
    const procSeed = dealSeed + 1n;
    lib.symbols.game_init(this.gamePtr, numPlayers, dealSeed, procSeed);

    // Resolve initial chance nodes (deal phase)
    this.resolveChance();
  }

  get numPlayers(): number {
    return this._numPlayers;
  }

  get phase(): Phase {
    // phase_state is uint16 at index 7 (players[6] + deck = 7 uint16s)
    const u16 = new Uint16Array(this.gameBuffer);
    return (u16[7] & 0xF) as Phase;
  }

  get turnPlayer(): number {
    const u16 = new Uint16Array(this.gameBuffer);
    return (u16[7] >> 4) & 0x7;
  }

  get activePlayer(): number {
    return getLib().symbols.get_active_player_ext(this.gamePtr);
  }

  get pendingAction(): number {
    const u16 = new Uint16Array(this.gameBuffer);
    return (u16[7] >> 10) & 0x3F;
  }

  get validMask(): number {
    return getLib().symbols.get_valid_actions(this.gamePtr);
  }

  get done(): boolean {
    return getLib().symbols.is_done(this.gamePtr) !== 0;
  }

  get winner(): number {
    return getLib().symbols.get_winner(this.gamePtr);
  }

  step(action: number) {
    getLib().symbols.step_with_rng(this.gamePtr, action);
    // step_with_rng handles chance nodes internally
  }

  stepDeterministic(action: number) {
    getLib().symbols.step_deterministic(this.gamePtr, action);
  }

  observe(playerId: number): Float32Array {
    // Pass null history buffer pointer (no history tracking in TUI)
    getLib().symbols.observe(this.gamePtr, playerId, 0 as any, this.obsPtr);
    return new Float32Array(this.obsBuffer);
  }

  isChanceNode(): boolean {
    return getLib().symbols.is_chance_node(this.gamePtr) !== 0;
  }

  private resolveChance() {
    const lib = getLib();
    while (lib.symbols.is_chance_node(this.gamePtr)) {
      // Use the built-in RNG by calling step_with_rng with action 0
      // Actually, chance nodes are resolved by apply_chance with a sampled outcome
      // For the TUI we can use the internal PRNG approach:
      // step_with_rng resolves chance nodes internally when called
      // But we're already past init... let's sample from chance_outcomes

      const outBuf = new ArrayBuffer(5 * 12); // 5 * (int + double padding)
      const outPtr = ptr(outBuf);
      const n = lib.symbols.chance_outcomes(this.gamePtr, outPtr);

      if (n <= 0) break;

      // Read outcomes and sample
      const view = new DataView(outBuf);
      let total = 0;
      const outcomes: { outcome: number; prob: number }[] = [];
      for (let i = 0; i < n; i++) {
        const outcome = view.getInt32(i * 12, true);
        const prob = view.getFloat64(i * 12 + 4, true);
        outcomes.push({ outcome, prob });
        total += prob;
      }

      // Weighted random sample
      let r = Math.random() * total;
      let chosen = outcomes[0].outcome;
      for (const o of outcomes) {
        r -= o.prob;
        if (r <= 0) {
          chosen = o.outcome;
          break;
        }
      }

      lib.symbols.apply_chance(this.gamePtr, chosen);
    }
  }

  // Direct state accessors via bit manipulation on the game struct
  getPlayerCoins(playerId: number): number {
    return (this.playerView[playerId] >> 8) & 0xF;
  }

  getPlayerCard(playerId: number, slot: number): CardType {
    if (slot === 0) return (this.playerView[playerId] & 0x7) as CardType;
    return ((this.playerView[playerId] >> 4) & 0x7) as CardType;
  }

  isCardAlive(playerId: number, slot: number): boolean {
    if (slot === 0) return ((this.playerView[playerId] >> 3) & 1) === 1;
    return ((this.playerView[playerId] >> 7) & 1) === 1;
  }

  isPlayerAlive(playerId: number): boolean {
    return this.isCardAlive(playerId, 0) || this.isCardAlive(playerId, 1);
  }

  getPlayerInfluence(playerId: number): number {
    let count = 0;
    if (this.isCardAlive(playerId, 0)) count++;
    if (this.isCardAlive(playerId, 1)) count++;
    return count;
  }

  getPlayerState(playerId: number): PlayerState {
    return {
      coins: this.getPlayerCoins(playerId),
      influence: this.getPlayerInfluence(playerId),
      cards: [
        { type: this.getPlayerCard(playerId, 0), alive: this.isCardAlive(playerId, 0) },
        { type: this.getPlayerCard(playerId, 1), alive: this.isCardAlive(playerId, 1) },
      ],
      alive: this.isPlayerAlive(playerId),
    };
  }

  // Exchange card accessors (aux field at u16 index 8)
  getExchangeCard(slot: number): CardType {
    const u16 = new Uint16Array(this.gameBuffer);
    const aux = u16[8];
    if (slot === 0) return ((aux >> 6) & 0x7) as CardType;
    return ((aux >> 9) & 0x7) as CardType;
  }

  getSnapshot(): GameSnapshot {
    const players: PlayerState[] = [];
    for (let i = 0; i < this._numPlayers; i++) {
      players.push(this.getPlayerState(i));
    }

    // Include exchange cards in snapshot during ExchangeDiscard phase
    let exchangeCards: [CardType, CardType] | undefined;
    if (this.phase === Phase.ExchangeDiscard) {
      exchangeCards = [this.getExchangeCard(0), this.getExchangeCard(1)];
    }

    return {
      phase: this.phase,
      activePlayer: this.activePlayer,
      turnPlayer: this.turnPlayer,
      pendingAction: this.pendingAction,
      players,
      numPlayers: this._numPlayers,
      done: this.done,
      winner: this.winner,
      validMask: this.validMask,
      exchangeCards,
    };
  }

  // Get valid actions as array of action indices
  getValidActions(): number[] {
    const mask = this.validMask;
    const actions: number[] = [];
    for (let i = 0; i < 32; i++) {
      if ((mask >> i) & 1) actions.push(i);
    }
    return actions;
  }
}
