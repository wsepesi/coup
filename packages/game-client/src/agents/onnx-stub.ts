// T13: ONNX stub — placeholder for neural bot inference

import type { Agent } from "../agent.js";

export class OnnxBot implements Agent {
  readonly name: string;

  constructor(id: number, _modelPath?: string) {
    this.name = `neural-${id}`;
    // Would load ONNX model here via onnxruntime-node
  }

  async chooseAction(_obs: Float32Array, validMask: number): Promise<number> {
    // Fallback to random since no model is available
    const actions: number[] = [];
    for (let i = 0; i < 32; i++) {
      if ((validMask >> i) & 1) actions.push(i);
    }
    return actions[Math.floor(Math.random() * actions.length)];
  }
}
