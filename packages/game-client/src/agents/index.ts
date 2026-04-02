export { EasyBot } from "./heuristic-easy.js";
export { MediumBot } from "./heuristic-medium.js";
export { HardBot } from "./heuristic-hard.js";
export { OnnxBot } from "./onnx-stub.js";

import { registry } from "../agent.js";
import { EasyBot } from "./heuristic-easy.js";
import { MediumBot } from "./heuristic-medium.js";
import { HardBot } from "./heuristic-hard.js";

let botCounter = 1;

export function createBot(difficulty: "easy" | "medium" | "hard", seat: number, numPlayers: number) {
  const id = botCounter++;
  switch (difficulty) {
    case "easy": return new EasyBot(id);
    case "medium": return new MediumBot(id, seat);
    case "hard": return new HardBot(id, seat, numPlayers);
  }
}

export function resetBotCounter() {
  botCounter = 1;
}
