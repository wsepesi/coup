// T25: Game loop + state machine

import { RGBA } from "@opentui/core";
import type { OptimizedBuffer } from "@opentui/core";
import type { FrameBufferRenderable } from "@opentui/core";
import type { KeyEvent } from "@opentui/core";
import {
  CoupGame,
  type Agent,
  type GameSnapshot,
  Phase,
  Action,
  actionTarget,
  actionCategory,
  claimedRole,
  CARD_NAMES,
  CardType,
  renderEvent,
  type GameEvent,
  describeAction,
} from "@coup/game-client";
import { createBot, resetBotCounter } from "@coup/game-client";

import { computePlayerPositions, getTableCenter, type PlayerPosition } from "./layout.js";
import { COLORS } from "./constants.js";
import { renderTable } from "./renderer/table.js";
import { renderCenter } from "./renderer/center.js";
import { renderHand } from "./renderer/hand.js";
import {
  getActionOptions,
  getTargetOptions,
  renderActionPanel,
  navigateGrid,
  type ActionOption,
} from "./renderer/actions.js";
import { HistoryTicker } from "./renderer/history-ticker.js";
import { renderGameOver, type GameResult } from "./renderer/game-over.js";
import { sleep, createSpinner } from "./animation/index.js";
import type { GameConfig } from "./setup.js";

export type GameOverChoice = "replay" | "new" | "quit";

export async function runGame(
  fb: FrameBufferRenderable,
  waitForKey: () => Promise<KeyEvent>,
  config: GameConfig,
  w: number,
  h: number,
  fast: boolean,
): Promise<GameOverChoice> {
  resetBotCounter();

  const seed = config.seed ? BigInt(config.seed) : BigInt(Math.floor(Math.random() * 2 ** 32));
  const seedStr = seed.toString();
  const numPlayers = config.players;
  const humanSeat = config.seat;

  const game = new CoupGame(numPlayers, seed);

  const agents: (Agent | null)[] = [];
  for (let i = 0; i < numPlayers; i++) {
    if (i === humanSeat) {
      agents.push(null);
    } else {
      agents.push(createBot(config.difficulty, i, numPlayers));
    }
  }

  const history = new HistoryTicker(3);
  let turnCount = 0;
  const eliminationOrder: { seat: number; turn: number }[] = [];
  let prevAlive = new Array(numPlayers).fill(true);

  // Layout: constrain zones to avoid overlap
  const tableH = Math.floor(h * 0.6);
  const handH = 7; // "You" label + 5-line card + coins
  const handY = tableH - handH;
  const centerMaxY = handY - 2; // center box must end above the hand
  const historyY = tableH;
  const historyH = 4;
  const actionY = historyY + historyH;
  const actionH = h - actionY;
  const positions = computePlayerPositions(numPlayers, w, h, tableH - handH);
  const center = getTableCenter(w, Math.floor((tableH - handH) * 0.9));

  const buf = fb.frameBuffer;

  function playerName(seat: number): string {
    if (seat === humanSeat) return "You";
    return agents[seat]?.name ?? `bot-${seat}`;
  }

  function isHuman(seat: number): boolean {
    return seat === humanSeat;
  }

  // Build a context string for what's happening (for center display during reactive phases)
  function actionContextText(snapshot: GameSnapshot): string[] {
    const tp = snapshot.turnPlayer;
    const pa = snapshot.pendingAction;
    const tpName = playerName(tp);
    const target = actionTarget(pa);
    const targetName = target != null ? playerName(target) : undefined;
    const desc = describeAction(pa, tpName, isHuman(tp), targetName);
    return [desc];
  }

  function redraw(
    snapshot: GameSnapshot,
    centerText?: string[],
    targetSeat?: number,
    selectedSlot?: number,
  ) {
    buf.clear(RGBA.fromHex(COLORS.bg));

    // Seed display
    const seedLabel = `seed: ${seedStr}`;
    buf.drawText(seedLabel, w - seedLabel.length - 1, 0, RGBA.fromHex(COLORS.textDim));

    // Table (other players) with turn indicator
    renderTable(buf, snapshot, positions, humanSeat, targetSeat, playerName);

    // Center text
    if (centerText) {
      renderCenter(buf, centerText, center.x, center.y);
    }

    // Your hand
    const me = snapshot.players[humanSeat];
    if (me) {
      const youLabel = `★ P${humanSeat} YOU ★`;
      buf.drawText(
        youLabel,
        Math.floor(w / 2) - Math.floor(youLabel.length / 2),
        handY,
        RGBA.fromHex(COLORS.you),
      );
      renderHand(buf, me.cards, me.coins, w, handY + 1, selectedSlot);
    }

    // History ticker
    history.render(buf, 0, historyY, w, historyH);
  }

  function trackEliminations(snapshot: GameSnapshot) {
    for (let i = 0; i < numPlayers; i++) {
      if (prevAlive[i] && !snapshot.players[i].alive) {
        eliminationOrder.push({ seat: i, turn: turnCount });
        history.push(
          renderEvent({
            type: "elimination",
            player: i,
            playerName: playerName(i),
            isHuman: isHuman(i),
          }),
        );
      }
    }
    prevAlive = snapshot.players.map((p) => p.alive);
  }

  function logAction(action: number, seat: number) {
    const target = actionTarget(action);
    const tName = target != null ? playerName(target) : undefined;
    const event = actionToEvent(action, seat, playerName(seat), isHuman(seat), target, tName, target != null ? isHuman(target) : undefined);
    if (event) {
      history.push(renderEvent(event));
    }
  }

  // === Main game loop ===
  while (!game.done) {
    const snapshot = game.getSnapshot();
    trackEliminations(snapshot);

    const active = snapshot.activePlayer;

    if (active === humanSeat) {
      const action = await humanTurn(game, snapshot, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, playerName, isHuman, history);
      logAction(action, humanSeat);
      game.step(action);
      if (snapshot.phase === Phase.MainAction) turnCount++;
    } else {
      await botTurn(game, snapshot, active, agents[active]!, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, history, playerName, isHuman, logAction, fast, positions, agents);
      if (snapshot.phase === Phase.MainAction) turnCount++;
    }
  }

  trackEliminations(game.getSnapshot());

  return showGameOver(buf, waitForKey, game, humanSeat, seedStr, turnCount, eliminationOrder, playerName, w, h);
}

// Build descriptive header text for reactive phases
function buildReactiveHeader(
  snapshot: GameSnapshot,
  playerName: (s: number) => string,
): string {
  const tp = snapshot.turnPlayer;
  const pa = snapshot.pendingAction;
  const tpName = playerName(tp);
  const target = actionTarget(pa);
  const targetName = target != null ? playerName(target) : undefined;

  const phase = snapshot.phase;

  if (phase === Phase.ChallengeAction) {
    const role = claimedRole(pa);
    const roleName = role != null ? CARD_NAMES[role] : "?";
    if (pa >= 10 && pa <= 15) return `${tpName} claims ${roleName} to Steal from ${targetName}. Challenge?`;
    if (pa >= 16 && pa <= 21) return `${tpName} claims ${roleName} to Assassinate ${targetName}. Challenge?`;
    if (pa === Action.Tax) return `${tpName} claims ${roleName} for Tax. Challenge?`;
    if (pa === Action.Exchange) return `${tpName} claims ${roleName} for Exchange. Challenge?`;
    return `${tpName} made a claim. Challenge?`;
  }

  if (phase === Phase.Block) {
    if (pa === Action.ForeignAid) return `${tpName} attempts Foreign Aid. Block with Duke?`;
    if (pa >= 10 && pa <= 15) return `${tpName} Steals from ${targetName}. Block?`;
    if (pa >= 16 && pa <= 21) return `${tpName} Assassinates ${targetName}. Block?`;
    return `${tpName} acted. Block?`;
  }

  if (phase === Phase.ChallengeBlock) {
    return `A block was declared. Challenge the block?`;
  }

  if (phase === Phase.LoseCard) return "You must lose an influence. Choose a card:";
  if (phase === Phase.ExchangeDiscard) return "Ambassador Exchange. Choose cards to discard:";

  return "Choose:";
}

async function humanTurn(
  game: CoupGame,
  snapshot: GameSnapshot,
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  humanSeat: number,
  w: number,
  h: number,
  actionY: number,
  actionH: number,
  redraw: Function,
  actionContextText: (snap: GameSnapshot) => string[],
  playerName: (s: number) => string,
  isHuman: (s: number) => boolean,
  history: HistoryTicker,
): Promise<number> {
  const phase = snapshot.phase;

  if (phase === Phase.MainAction) {
    const options = getActionOptions(snapshot, humanSeat);
    const headerText =
      snapshot.players[humanSeat].coins >= 10
        ? "You must Coup (10+ coins)."
        : "Your turn. Choose an action:";

    const result = await selectFromGrid(buf, waitForKey, options, headerText, false, w, actionY, actionH, history, h, (idx) => {
      redraw(snapshot, ["Your turn..."], undefined);
    });

    const chosen = options[result];
    if (!chosen) return Action.Income;

    if (chosen.action < 0) {
      const actionType = chosen.action === -1 ? "coup" as const : chosen.action === -2 ? "steal" as const : "assassinate" as const;
      const targets = getTargetOptions(snapshot, actionType, humanSeat);
      if (targets.length === 0) return Action.Income;

      const targetHeader = `${chosen.label} → choose target:`;
      const targetResult = await selectFromGrid(buf, waitForKey, targets, targetHeader, true, w, actionY, actionH, history, h, (idx) => {
        const tSeat = actionTarget(targets[idx]?.action ?? 0);
        redraw(snapshot, [`Select target for`, chosen.label], tSeat ?? undefined);
      });

      if (targetResult === -1) {
        return humanTurn(game, snapshot, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, playerName, isHuman, history);
      }

      return targets[targetResult]?.action ?? Action.Income;
    }

    return chosen.action;
  }

  // Reactive phases (challenge/block/lose card/exchange)
  const options = getActionOptions(snapshot, humanSeat);
  if (options.length === 0) return Action.Pass;

  const headerText = buildReactiveHeader(snapshot, playerName);

  // Default to Pass if available
  const passIdx = options.findIndex(o => o.action === Action.Pass);
  const initialIdx = passIdx >= 0 ? passIdx : 0;

  // Center shows the action context
  const centerText = actionContextText(snapshot);

  const result = await selectFromGrid(buf, waitForKey, options, headerText, false, w, actionY, actionH, history, h, (idx) => {
    const selectedSlot =
      phase === Phase.LoseCard && options[idx]
        ? options[idx].action - Action.DiscardSlot0
        : undefined;
    redraw(snapshot, centerText, undefined, selectedSlot);
  }, initialIdx);

  return options[result]?.action ?? Action.Pass;
}

async function selectFromGrid(
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  options: ActionOption[],
  headerText: string,
  allowEsc: boolean,
  w: number,
  actionY: number,
  actionH: number,
  history: HistoryTicker,
  screenH: number,
  onIndexChange: (idx: number) => void,
  initialIdx: number = 0,
): Promise<number> {
  let idx = initialIdx;

  function render() {
    onIndexChange(idx);
    renderActionPanel(buf, options, idx, headerText, 0, actionY, w, actionH, allowEsc);
  }

  render();

  while (true) {
    const key = await waitForKey();
    const name = key.name;

    if (name === "up") idx = navigateGrid(idx, "up", options.length);
    else if (name === "down") idx = navigateGrid(idx, "down", options.length);
    else if (name === "left") idx = navigateGrid(idx, "left", options.length);
    else if (name === "right") idx = navigateGrid(idx, "right", options.length);
    else if (name === "return") return idx;
    else if (name === "escape" && allowEsc) return -1;
    else if (name === "h") {
      // Open full history viewer
      await showFullHistory(buf, waitForKey, history, w, screenH);
      render(); // re-render after closing history
      continue;
    }
    else if (/^[1-9]$/.test(name)) {
      const numIdx = parseInt(name) - 1;
      if (numIdx < options.length) return numIdx;
    }

    render();
  }
}

async function showFullHistory(
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  history: HistoryTicker,
  w: number,
  h: number,
) {
  let scrollOffset = Math.max(0, history.length - (h - 3));

  function render() {
    history.renderFullHistory(buf, scrollOffset, w, h);
  }

  render();

  while (true) {
    const key = await waitForKey();
    const name = key.name;
    if (name === "h" || name === "escape") return;
    if (name === "up") scrollOffset = Math.max(0, scrollOffset - 1);
    else if (name === "down") scrollOffset++;
    render();
  }
}

async function botTurn(
  game: CoupGame,
  snapshot: GameSnapshot,
  botSeat: number,
  agent: Agent,
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  humanSeat: number,
  w: number,
  h: number,
  actionY: number,
  actionH: number,
  redraw: Function,
  actionContextText: (snap: GameSnapshot) => string[],
  history: HistoryTicker,
  playerName: (s: number) => string,
  isHuman: (s: number) => boolean,
  logAction: (a: number, s: number) => void,
  fast: boolean,
  positions: PlayerPosition[],
  agents: (Agent | null)[],
) {
  const botName = playerName(botSeat);

  // Thinking display
  redraw(snapshot, [`${botName} is`, "thinking..."]);
  const pos = positions.find((p) => p.seat === botSeat);
  const spinner = pos ? createSpinner(buf, pos.x + 12, pos.y, fast) : null;

  await sleep(300 + Math.random() * 300, fast);
  spinner?.stop();

  // Bot decides
  const obs = game.observe(botSeat);
  const mask = game.validMask;
  const action = await agent.chooseAction(obs, mask);

  logAction(action, botSeat);
  game.step(action);

  // Show result
  const newSnap = game.getSnapshot();
  const desc = describeAction(action, botName, false, actionTarget(action) != null ? playerName(actionTarget(action)!) : undefined);
  redraw(newSnap, [desc]);

  await sleep(200, fast);

  // Handle follow-up phases
  while (!game.done) {
    const currentSnap = game.getSnapshot();
    const currentActive = currentSnap.activePlayer;

    if (currentSnap.phase === Phase.MainAction) break;

    if (currentActive === humanSeat) {
      const humanAction = await humanTurn(
        game, currentSnap, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, playerName, isHuman, history,
      );
      logAction(humanAction, humanSeat);
      game.step(humanAction);
      redraw(game.getSnapshot());
      await sleep(150, fast);
    } else {
      // Another bot responds
      const respAgent = agents[currentActive];
      if (!respAgent) break;
      const obs2 = game.observe(currentActive);
      const mask2 = game.validMask;
      const action2 = await respAgent.chooseAction(obs2, mask2);
      logAction(action2, currentActive);
      game.step(action2);
      redraw(game.getSnapshot());
      await sleep(150, fast);
    }
  }
}

async function showGameOver(
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  game: CoupGame,
  humanSeat: number,
  seedStr: string,
  turnCount: number,
  eliminationOrder: { seat: number; turn: number }[],
  playerName: (s: number) => string,
  w: number,
  h: number,
): Promise<GameOverChoice> {
  const winner = game.winner;

  const placements: GameResult["placements"] = [];
  placements.push({ seat: winner, name: playerName(winner) });
  for (let i = eliminationOrder.length - 1; i >= 0; i--) {
    const e = eliminationOrder[i];
    placements.push({
      seat: e.seat,
      name: playerName(e.seat),
      eliminatedTurn: e.turn,
    });
  }

  const result: GameResult = {
    winner,
    winnerName: playerName(winner),
    placements,
    seed: seedStr,
    totalTurns: turnCount,
  };

  renderGameOver(buf, result, w, h);

  while (true) {
    const key = await waitForKey();
    const name = key.name?.toLowerCase();
    if (name === "r") return "replay";
    if (name === "n") return "new";
    if (name === "q") return "quit";
  }
}

function actionToEvent(
  action: number,
  player: number,
  pName: string,
  pIsHuman: boolean,
  target: number | null,
  tName?: string,
  tIsHuman?: boolean,
): GameEvent | null {
  const base = { player, playerName: pName, isHuman: pIsHuman };
  const withTarget = { ...base, target: target!, targetName: tName, targetIsHuman: tIsHuman };

  if (action === Action.Income)
    return { ...base, type: "income" };
  if (action === Action.ForeignAid)
    return { ...base, type: "foreign_aid" };
  if (action === Action.Tax)
    return { ...base, type: "tax" };
  if (action === Action.Exchange)
    return { ...base, type: "exchange" };
  if (action >= Action.CoupP0 && action <= Action.CoupP5)
    return { ...withTarget, type: "coup" };
  if (action >= Action.StealP0 && action <= Action.StealP5)
    return { ...withTarget, type: "steal" };
  if (action >= Action.AssassinateP0 && action <= Action.AssassinateP5)
    return { ...withTarget, type: "assassinate" };
  if (action === Action.Challenge)
    return { ...base, type: "challenge" };
  if (action === Action.Pass)
    return { ...base, type: "pass" };
  if (action >= Action.BlockContessa && action <= Action.BlockDuke)
    return { ...base, type: "block" };
  if (action >= Action.DiscardSlot0 && action <= Action.DiscardSlot3)
    return { ...base, type: "lose_card" };
  return null;
}
