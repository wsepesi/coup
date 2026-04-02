// T10: Easy bot — random legal action, never challenges/blocks

import type { Agent } from "../agent.js";
import { Action } from "../types.js";

export class EasyBot implements Agent {
  readonly name: string;

  constructor(id: number) {
    this.name = `bot-${id}`;
  }

  async chooseAction(_obs: Float32Array, validMask: number): Promise<number> {
    // Prefer pass over challenge in challenge/block phases
    const actions: number[] = [];
    for (let i = 0; i < 32; i++) {
      if ((validMask >> i) & 1) actions.push(i);
    }

    // If pass is available, always pass (never challenge/block)
    if (actions.includes(Action.Pass)) {
      return Action.Pass;
    }

    // Random from valid actions
    return actions[Math.floor(Math.random() * actions.length)];
  }
}
