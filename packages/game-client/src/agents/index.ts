export { EasyBot } from "./heuristic-easy.js";
export { MediumBot } from "./heuristic-medium.js";
export { HardBot } from "./heuristic-hard.js";
export { OnnxBot } from "./onnx-stub.js";

import { registry } from "../agent.js";
import { EasyBot } from "./heuristic-easy.js";
import { MediumBot } from "./heuristic-medium.js";
import { HardBot } from "./heuristic-hard.js";

const MII_NAMES = [
  "Matt", "Lucia", "Elisa", "Tyrone", "Abby", "Ren", "Sakura", "Pierre",
  "Haru", "Marco", "Emily", "Takumi", "Miyu", "Oscar", "Silke", "Theo",
  "Naomi", "Kenji", "Gabi", "Luca", "Yuki", "Steph", "Akira", "Dina",
  "Tommy", "Mia", "Fritz", "Elena", "Ravi", "Anna",
];

let namePool: string[] = [];

function shuffleNames() {
  namePool = [...MII_NAMES];
  for (let i = namePool.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [namePool[i], namePool[j]] = [namePool[j], namePool[i]];
  }
}

export function createBot(difficulty: "easy" | "medium" | "hard", seat: number, numPlayers: number) {
  if (namePool.length === 0) shuffleNames();
  const name = namePool.pop()!;
  switch (difficulty) {
    case "easy": return new EasyBot(name);
    case "medium": return new MediumBot(name, seat);
    case "hard": return new HardBot(name, seat, numPlayers);
  }
}

export function resetBotCounter() {
  shuffleNames();
}
