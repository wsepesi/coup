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
  claimedRole,
  blockCardType,
  CARD_NAMES,
  CardType,
  renderEvent,
  type GameEvent,
  describeAction,
} from "@coup/game-client";
import { createBot, initBots } from "@coup/game-client";

import { computePlayerPositions, getTableCenter, type PlayerPosition } from "./layout.js";
import { COLORS, getCardColors, CARD_ABBREV } from "./constants.js";
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
import { renderDeckTracker, type DeckEvent } from "./renderer/deck-tracker.js";
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
  const seed = config.seed ? BigInt(config.seed) : BigInt(Math.floor(Math.random() * 2 ** 32));
  initBots(seed);
  const seedStr = seed.toString();
  const numPlayers = config.players;
  const humanSeat = config.seat === -2
    ? Math.floor(Math.random() * numPlayers)
    : config.seat;

  const game = new CoupGame(numPlayers, seed);
  if (config.houseRules && !config.houseRules.refundOnChallenge) {
    game.setRefundOnChallenge(false);
  }

  const agents: (Agent | null)[] = [];
  for (let i = 0; i < numPlayers; i++) {
    if (i === humanSeat && humanSeat >= 0 && humanSeat < numPlayers) {
      agents.push(null);
    } else {
      agents.push(createBot(config.difficulty, i, numPlayers));
    }
  }

  // Layout: bottom-up allocation so action panel + hand are always visible
  const actionH = 6;  // separator + header + blank + 2 rows of options + padding
  const handH = 8;    // "You" label + 5-line card + coins + claimed cards
  // History: scale with screen, but guarantee at least 2 lines
  const historyLines = h < 30 ? 2 : Math.max(4, Math.min(12, Math.floor(h * 0.20)));
  const historyH = historyLines + 1; // +1 for separator
  // Table zone: everything above the fixed bottom sections
  const tableH = Math.max(6, h - actionH - historyH - handH);

  const actionY = h - actionH;
  const historyY = actionY - historyH;
  const handY = historyY - handH;

  const history = new HistoryTicker(historyLines);
  let turnCount = 0;
  const eliminationOrder: { seat: number; turn: number }[] = [];
  let prevAlive = new Array(numPlayers).fill(true);

  // Claims tracking: seat → set of claimed card types
  const claimsMap = new Map<number, Set<CardType>>();

  // Last main action per player (persists between turns)
  const lastActionMap = new Map<number, number>();

  // Pass collapsing: buffer consecutive pass names, flush as one line
  const pendingPasses: { name: string; isYou: boolean }[] = [];
  function flushPasses() {
    if (pendingPasses.length === 0) return;
    const entries = pendingPasses.splice(0);
    if (entries.length === 1) {
      const verb = entries[0].isYou ? "pass" : "passes";
      history.push(`${entries[0].name} ${verb}.`);
    } else {
      history.push(`${entries.map(e => e.name).join(", ")} pass.`);
    }
  }

  // Deck movement tracker
  const deckEvents: DeckEvent[] = [];

  // Speed control for spectating/watching
  const SPEED_LEVELS = [
    { name: "Slow",  mult: 1.5 },
    { name: "Med",   mult: 1.0 },
    { name: "Fast",  mult: 0.3 },
    { name: "Ultra", mult: 0.08 },
    { name: "FF",    mult: 0 },
  ] as const;
  let speedIdx = 1; // default: Med
  let fastForward = fast;
  if (fast) speedIdx = SPEED_LEVELS.length - 1;
  let humanElimPromptShown = false;
  let godMode = false;
  const spectating = humanSeat < 0 || humanSeat >= numPlayers;

  function currentSpeedName(): string {
    return SPEED_LEVELS[speedIdx].name;
  }

  // Sleep that can be interrupted by speed change keys (when spectating/eliminated)
  async function interruptibleSleep(ms: number) {
    if (fastForward || SPEED_LEVELS[speedIdx].mult === 0) return;
    const scaled = Math.round(ms * SPEED_LEVELS[speedIdx].mult);
    if (scaled <= 0) return;
    const timeout = new Promise<void>(r => setTimeout(r, scaled));
    const keyPress = waitForKey().then(k => {
      if (k.name === "f") { speedIdx = SPEED_LEVELS.length - 1; fastForward = true; }
      else if (k.name === "," || k.name === "<") { speedIdx = Math.max(0, speedIdx - 1); }
      else if (k.name === "." || k.name === ">") { speedIdx = Math.min(SPEED_LEVELS.length - 1, speedIdx + 1); }
      else if (k.name === "g") { godMode = !godMode; }
    });
    await Promise.race([timeout, keyPress]);
  }

  // Table zone: row 1 to handY-1 (row 0 for seed label)
  const tableTop = 1;
  const tableZoneH = Math.max(4, handY - tableTop);
  // Cap effective zone so opponents don't spread too thin on large screens
  const effectiveZoneH = Math.min(tableZoneH, 22);
  // Opponent positions in upper portion of table zone (humanSeat excluded)
  const positions = computePlayerPositions(numPlayers, w, effectiveZoneH, humanSeat);
  // Offset positions down by tableTop so they start below the seed line
  for (const p of positions) {
    p.y += tableTop;
  }
  // Center box positioned just below the opponents
  const lowestOpponent = positions.length > 0
    ? Math.max(...positions.map(p => p.y)) + 3 // +3 for player display height + gap
    : tableTop + 2;
  const centerBoxH = Math.min(7, Math.max(3, handY - lowestOpponent - 1));
  const centerBoxW = Math.min(38, w < 100 ? 28 : w - 4);
  const centerY = Math.min(
    lowestOpponent + Math.ceil(centerBoxH / 2) + 1,
    handY - Math.ceil(centerBoxH / 2) - 1,
  );
  const center = { x: Math.floor(w / 2), y: centerY };

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

    // Table (other players) with turn indicator and claimed cards
    renderTable(buf, snapshot, positions, humanSeat, targetSeat, playerName, claimsMap, lastActionMap, godMode);

    // Center text
    if (centerText) {
      renderCenter(buf, centerText, center.x, center.y, centerBoxW, centerBoxH);
    }

    // Deck tracker (bottom-left of table zone)
    const trackerW = 22;
    const trackerH = Math.min(8, Math.max(4, handY - tableTop - 2));
    const trackerX = 1;
    const trackerY = Math.max(tableTop, handY - trackerH);
    if (trackerH >= 3 && trackerY >= tableTop) {
      renderDeckTracker(buf, snapshot.deckSize, deckEvents, trackerX, trackerY, trackerW, trackerH);
    }

    // God mode: deck composition
    if (godMode && snapshot.deckCards) {
      const cardColors = getCardColors();
      const deckY = (trackerH >= 3 && trackerY >= tableTop) ? trackerY + trackerH : handY - 2;
      let dx = trackerX;
      buf.drawText("Deck:", dx, deckY, RGBA.fromHex(COLORS.textBright));
      dx += 6;
      for (const ct of [CardType.Duke, CardType.Assassin, CardType.Captain, CardType.Ambassador, CardType.Contessa]) {
        const abbrev = CARD_ABBREV[ct];
        const count = snapshot.deckCards[ct] ?? 0;
        buf.drawText(`${abbrev}:${count}`, dx, deckY, RGBA.fromHex(cardColors[ct]));
        dx += abbrev.length + 2 + 1; // abbrev + ":N" + space
      }
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

      // Show own claimed cards below coins
      const myClaims = claimsMap.get(humanSeat);
      if (myClaims && myClaims.size > 0) {
        const cardColors = getCardColors();
        const abbrevs = Array.from(myClaims).map(ct => ({ ct, ab: CARD_ABBREV[ct] }));
        const totalLen = abbrevs.reduce((s, a) => s + a.ab.length + 1, -1);
        let cx = Math.floor(w / 2) - Math.floor(totalLen / 2);
        for (const { ct, ab } of abbrevs) {
          buf.drawText(ab, cx, handY + 7, RGBA.fromHex(cardColors[ct]));
          cx += ab.length + 1;
        }
      }
    }

    // History ticker
    history.render(buf, 0, historyY, w, historyH);

    // Spectator hint with speed indicator
    if (spectating || (!spectating && !snapshot.players[humanSeat]?.alive)) {
      const speed = `[${currentSpeedName()}]`;
      const godLabel = godMode ? " [GOD]" : "";
      const hint = fastForward ? "" : `< > speed ${speed}   (F) skip to end   (G) god mode${godLabel}   (H) history`;
      buf.drawText(hint, Math.floor(w / 2) - Math.floor(hint.length / 2), actionY + 2, RGBA.fromHex(COLORS.textDim));
      if (godMode && !fastForward) {
        buf.drawText("[GOD]", Math.floor(w / 2) - Math.floor(hint.length / 2) + hint.indexOf("[GOD]"), actionY + 2, RGBA.fromHex(COLORS.cursor));
      }
    }
  }

  function trackEliminations(snapshot: GameSnapshot) {
    for (let i = 0; i < numPlayers; i++) {
      if (prevAlive[i] && !snapshot.players[i].alive) {
        flushPasses();
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

  // Track challenge state for detecting reshuffles
  let pendingChallenge: { challenger: number; claimant: number; claimedCard: CardType } | null = null;

  function logAction(action: number, seat: number, phase?: Phase) {
    // Track claims
    const role = claimedRole(action);
    if (role != null) {
      if (!claimsMap.has(seat)) claimsMap.set(seat, new Set());
      claimsMap.get(seat)!.add(role);
    }
    const bCard = blockCardType(action);
    if (bCard != null) {
      if (!claimsMap.has(seat)) claimsMap.set(seat, new Set());
      claimsMap.get(seat)!.add(bCard);
    }

    // Track last main action per player
    if (phase === Phase.MainAction && action >= Action.Income && action <= Action.AssassinateP5) {
      lastActionMap.set(seat, action);
    }

    // Suppress individual discard logs during exchange — logged as single event after
    if (phase === Phase.ExchangeDiscard && action >= Action.DiscardSlot0 && action <= Action.DiscardSlot3) {
      return;
    }

    // Collapse consecutive passes into one line
    if (action === Action.Pass) {
      pendingPasses.push({ name: playerName(seat), isYou: isHuman(seat) });
      return;
    }

    // Flush any buffered passes before logging a non-pass event
    flushPasses();

    let target = actionTarget(action);
    // For challenges, determine target based on phase
    let challengedCard: CardType | undefined;
    if (action === Action.Challenge && target == null) {
      const snap = game.getSnapshot();
      if (snap.phase === Phase.ChallengeBlock && snap.blocker != null && snap.blockCard != null) {
        // Challenging the block — target is the blocker, card is the block card
        target = snap.blocker;
        challengedCard = snap.blockCard;
        pendingChallenge = { challenger: seat, claimant: snap.blocker, claimedCard: snap.blockCard };
      } else {
        // Challenging the original action — target is the turn player
        target = snap.turnPlayer;
        const claimed = claimedRole(snap.pendingAction);
        if (claimed != null) {
          challengedCard = claimed;
          pendingChallenge = { challenger: seat, claimant: snap.turnPlayer, claimedCard: claimed };
        }
      }
    }

    // Detect challenge reshuffle: when lose_card happens to the challenger, challenge failed
    // → claimant's card gets reshuffled into deck
    if (action >= Action.DiscardSlot0 && action <= Action.DiscardSlot3 && pendingChallenge) {
      if (seat === pendingChallenge.challenger) {
        // Challenge failed — claimant reveals and reshuffles
        deckEvents.push({
          card: pendingChallenge.claimedCard,
          direction: "in",
          description: "reshuffled",
        });
        deckEvents.push({
          direction: "out",
          description: "drew replacement",
        });
        const cardName = CARD_NAMES[pendingChallenge.claimedCard];
        history.push(`Challenge failed! ${playerName(pendingChallenge.claimant)} reveals ${cardName}. (shuffled back, drew replacement)`);
      } else if (seat === pendingChallenge.claimant) {
        history.push(`Challenge succeeded! ${playerName(pendingChallenge.claimant)} was bluffing.`);
      }
      pendingChallenge = null;
    }

    const tName = target != null ? playerName(target) : undefined;
    const event = actionToEvent(action, seat, playerName(seat), isHuman(seat), target, tName, target != null ? isHuman(target) : undefined);
    if (event) {
      if (challengedCard != null) event.card = challengedCard;
      history.push(renderEvent(event));
    }
  }

  // === Main game loop ===
  while (!game.done) {
    const snapshot = game.getSnapshot();
    trackEliminations(snapshot);

    const active = snapshot.activePlayer;

    // Check if human was just eliminated — offer fast-forward
    const humanAlive = !spectating && snapshot.players[humanSeat]?.alive;
    if (!spectating && !humanAlive && !humanElimPromptShown) {
      humanElimPromptShown = true;
      redraw(snapshot, ["You have been", "eliminated!"]);
      await sleep(500, false);
      const promptText = "F skip to end | < > adjust speed | any key to watch";
      buf.drawText(promptText, Math.floor(w / 2) - Math.floor(promptText.length / 2), actionY + 2, RGBA.fromHex(COLORS.textBright));
      const key = await waitForKey();
      if (key.name === "f") {
        speedIdx = SPEED_LEVELS.length - 1;
        fastForward = true;
      }
    }

    if (!spectating && active === humanSeat && humanAlive) {
      // Track exchange draws when entering exchange discard phase
      if (snapshot.phase === Phase.ExchangeDiscard && snapshot.exchangeCards && !_pendingExchangeSlot) {
        deckEvents.push({ card: snapshot.exchangeCards[0], direction: "out", description: "drawn" });
        deckEvents.push({ card: snapshot.exchangeCards[1], direction: "out", description: "drawn" });
      }
      const hadPending = _pendingExchangeSlot != null;
      const action = await humanTurn(game, snapshot, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, playerName, isHuman, history, _pendingExchangeSlot);
      if (hadPending) _pendingExchangeSlot = null;
      // Track exchange returns: the discarded card goes back to deck
      if (snapshot.phase === Phase.ExchangeDiscard && action >= Action.DiscardSlot0 && action <= Action.DiscardSlot3) {
        const slot = action - Action.DiscardSlot0;
        const player = snapshot.players[humanSeat];
        let discardedType: CardType | undefined;
        if (slot < 2 && player?.cards[slot]) {
          discardedType = player.cards[slot].type;
        } else if (snapshot.exchangeCards) {
          const exIdx = slot - 2;
          if (exIdx >= 0 && exIdx < 2) discardedType = snapshot.exchangeCards[exIdx];
        }
        if (discardedType != null) {
          deckEvents.push({ card: discardedType, direction: "in", description: "returned" });
        }
      }
      logAction(action, humanSeat, snapshot.phase);
      game.step(action);
      // Log exchange completion when phase transitions away
      if (snapshot.phase === Phase.ExchangeDiscard) {
        const newPhase = game.getSnapshot().phase;
        if (newPhase !== Phase.ExchangeDiscard) {
          history.push(`You complete Exchange.`);
        }
      }
      if (snapshot.phase === Phase.MainAction) turnCount++;
    } else if (!spectating && active === humanSeat && !humanAlive) {
      // Dead human should not be active — skip
      break;
    } else {
      const humanAliveNow = humanSeat >= 0 && humanSeat < numPlayers && snapshot.players[humanSeat].alive;
      const doSleep = humanAliveNow
        ? (ms: number) => sleep(ms, false)
        : (ms: number) => interruptibleSleep(ms);
      await botTurn(game, snapshot, active, agents[active]!, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, history, playerName, isHuman, logAction, doSleep, positions, agents, deckEvents);
      if (snapshot.phase === Phase.MainAction) turnCount++;
    }
  }

  trackEliminations(game.getSnapshot());

  return showGameOver(buf, waitForKey, game, humanSeat, seedStr, turnCount, eliminationOrder, playerName, w, h, history);
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
  pendingExchangeSlotRef?: number | null,
): Promise<number> {
  const phase = snapshot.phase;

  // If we have a pending second exchange discard, return it immediately
  if (phase === Phase.ExchangeDiscard && pendingExchangeSlotRef != null) {
    return pendingExchangeSlotRef;
  }

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

  // Ambassador Exchange: show all 4 cards, pick 2 to discard
  if (phase === Phase.ExchangeDiscard) {
    return await handleExchangeDiscard(buf, waitForKey, snapshot, humanSeat, w, actionY, actionH, redraw, history, h);
  }

  // Reactive phases (challenge/block/lose card)
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

async function handleExchangeDiscard(
  buf: OptimizedBuffer,
  waitForKey: () => Promise<KeyEvent>,
  snapshot: GameSnapshot,
  humanSeat: number,
  w: number,
  actionY: number,
  actionH: number,
  redraw: Function,
  history: HistoryTicker,
  screenH: number,
): Promise<number> {
  const player = snapshot.players[humanSeat];
  const cardColors = getCardColors();

  interface ExCard { type: CardType; slot: number; source: "yours" | "drawn"; }
  const allCards: ExCard[] = [];
  for (let i = 0; i < 2; i++) {
    if (player.cards[i].alive) {
      allCards.push({ type: player.cards[i].type, slot: i, source: "yours" });
    }
  }
  if (snapshot.exchangeCards) {
    allCards.push({ type: snapshot.exchangeCards[0], slot: 2, source: "drawn" });
    allCards.push({ type: snapshot.exchangeCards[1], slot: 3, source: "drawn" });
  }

  const yoursCount = allCards.filter(c => c.source === "yours").length;
  const keepCount = yoursCount; // keep as many as you currently have alive
  const selected = new Set<number>();
  let cursor = 0;
  const cardW = 11;
  const gap = 1;
  const totalW = allCards.length * cardW + (allCards.length - 1) * gap;
  const startX = Math.floor(w / 2) - Math.floor(totalW / 2);

  function render() {
    redraw(snapshot, ["Ambassador Exchange", "Select 2 to KEEP"]);

    // Draw separator
    buf.drawText("═".repeat(w), 0, actionY, RGBA.fromHex(COLORS.border));

    const remaining = keepCount - selected.size;
    const hint = remaining === 0
      ? `Select ${keepCount} to KEEP:  ↑↓◀▶ move  Space toggle  Enter confirm`
      : `Select ${keepCount} to KEEP:  ↑↓◀▶ move  Space toggle (${remaining} more)`;
    buf.drawText(hint, 2, actionY + 1, RGBA.fromHex(COLORS.textDefault));
    buf.drawText("(H) history", w - 14, actionY + 1, RGBA.fromHex(COLORS.textDim));

    // Horizontal card layout: "[X] ▸Captain (yours)   [ ] Duke (drawn)  ..."
    // Build each item, measure total width, center the row
    const items: { text: string; cardName: string; cardColor: string; isCursor: boolean; isSel: boolean; src: string }[] = [];
    let totalItemW = 0;
    const colSpacing = 4;
    for (let i = 0; i < allCards.length; i++) {
      const card = allCards[i];
      const isCursor = i === cursor;
      const isSel = selected.has(i);
      const checkbox = isSel ? "[X] " : "[ ] ";
      const prefix = isCursor ? "▸ " : "  ";
      const name = CARD_NAMES[card.type];
      const src = `(${card.source})`;
      const text = `${prefix}${checkbox}${name} ${src}`;
      items.push({ text, cardName: name, cardColor: cardColors[card.type], isCursor, isSel, src });
      totalItemW += text.length;
    }
    totalItemW += colSpacing * (items.length - 1);

    // Row 1: yours cards. Row 2: drawn cards.
    const yoursItems = items.filter((_, i) => allCards[i].source === "yours");
    const drawnItems = items.filter((_, i) => allCards[i].source === "drawn");

    function drawRow(rowItems: typeof items, rowY: number) {
      let rowW = 0;
      for (const it of rowItems) rowW += it.text.length;
      rowW += colSpacing * Math.max(0, rowItems.length - 1);
      let cx = Math.floor(w / 2) - Math.floor(rowW / 2);

      for (const it of rowItems) {
        const bg = RGBA.fromHex(COLORS.bg);
        const rowBg = it.isCursor ? RGBA.fromHex(COLORS.selectionBg) : bg;
        if (it.isCursor) {
          buf.fillRect(cx, rowY, it.text.length + 1, 1, rowBg);
        }
        // prefix
        const prefixLen = it.isCursor ? 2 : 2;
        const prefixText = it.isCursor ? "▸ " : "  ";
        buf.drawText(prefixText, cx, rowY, RGBA.fromHex(COLORS.textBright), rowBg);
        // checkbox
        const cbText = it.isSel ? "[X] " : "[ ] ";
        const cbColor = it.isSel ? RGBA.fromHex(COLORS.cursor) : RGBA.fromHex(COLORS.textDim);
        buf.drawText(cbText, cx + prefixLen, rowY, cbColor, rowBg);
        // card name
        buf.drawText(it.cardName, cx + prefixLen + cbText.length, rowY, RGBA.fromHex(it.cardColor), rowBg);
        // source
        buf.drawText(` ${it.src}`, cx + prefixLen + cbText.length + it.cardName.length, rowY, RGBA.fromHex(COLORS.textDim), rowBg);

        cx += it.text.length + colSpacing;
      }
    }

    drawRow(yoursItems, actionY + 3);
    drawRow(drawnItems, actionY + 4);
  }

  render();

  while (true) {
    const key = await waitForKey();
    if (key.name === "left" || key.name === "right") {
      const next = key.name === "left" ? cursor - 1 : cursor + 1;
      if (next >= 0 && next < allCards.length) cursor = next;
    } else if (key.name === "up" || key.name === "down") {
      // Jump between yours row and drawn row
      if (key.name === "down" && cursor < yoursCount) {
        // Move from yours row to drawn row (same column position)
        const col = cursor;
        const drawnIdx = yoursCount + Math.min(col, allCards.length - yoursCount - 1);
        cursor = drawnIdx;
      } else if (key.name === "up" && cursor >= yoursCount) {
        // Move from drawn row to yours row
        const col = cursor - yoursCount;
        cursor = Math.min(col, yoursCount - 1);
      }
    }
    else if (key.name === " " || key.name === "space") {
      if (selected.has(cursor)) {
        selected.delete(cursor);
      } else if (selected.size < keepCount) {
        selected.add(cursor);
      }
    } else if (key.name === "return" && selected.size === keepCount) {
      // Discard the UNselected cards (selected = keep)
      const discardSlots = allCards
        .map((c, i) => ({ slot: c.slot, idx: i }))
        .filter(({ idx }) => !selected.has(idx))
        .map(({ slot }) => slot)
        .sort((a, b) => a - b);
      _pendingExchangeSlot = Action.DiscardSlot0 + discardSlots[1];
      return Action.DiscardSlot0 + discardSlots[0];
    } else if (key.name === "h") {
      await showFullHistory(buf, waitForKey, history, w, screenH);
    }
    render();
  }
}

// Module-level side channel for exchange second discard
let _pendingExchangeSlot: number | null = null;

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
    else if (name === "c") {
      const text = history.getAllEntries().map((e, i) => `${i + 1}. ${e}`).join("\n");
      try {
        const cmd = process.platform === "darwin" ? "pbcopy" : process.platform === "win32" ? "clip" : "xclip -selection clipboard";
        const proc = Bun.spawn(cmd.split(" "), { stdin: "pipe" });
        proc.stdin.write(text);
        proc.stdin.end();
        await proc.exited;
        // Flash "Copied!" feedback
        const bg = RGBA.fromHex(COLORS.bg);
        const bright = RGBA.fromHex(COLORS.textBright);
        const msg = " Copied to clipboard! ";
        buf.drawText(msg, Math.floor((w - msg.length) / 2), Math.floor(h / 2), bright, bg);
        await new Promise(r => setTimeout(r, 800));
      } catch {
        // silently ignore if clipboard not available
      }
    }
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
  logAction: (a: number, s: number, p?: Phase) => void,
  doSleep: (ms: number) => Promise<void>,
  positions: PlayerPosition[],
  agents: (Agent | null)[],
  deckEvents: DeckEvent[],
) {
  const botName = playerName(botSeat);

  // Thinking display
  redraw(snapshot, [`${botName} is`, "thinking..."]);
  const pos = positions.find((p) => p.seat === botSeat);
  const spinner = pos ? createSpinner(buf, pos.x + 12, pos.y, false) : null;

  await doSleep(300 + Math.random() * 300);
  spinner?.stop();

  // Bot decides
  const obs = game.observe(botSeat);
  const mask = game.validMask;
  const action = await agent.chooseAction(obs, mask);

  logAction(action, botSeat, snapshot.phase);
  game.step(action);

  // Show result
  const newSnap = game.getSnapshot();
  const desc = describeAction(action, botName, false, actionTarget(action) != null ? playerName(actionTarget(action)!) : undefined);
  redraw(newSnap, [desc]);

  await doSleep(200);

  // Handle follow-up phases
  while (!game.done) {
    const currentSnap = game.getSnapshot();
    const currentActive = currentSnap.activePlayer;

    if (currentSnap.phase === Phase.MainAction) break;

    if (currentActive === humanSeat) {
      // Track exchange draws when human enters exchange discard (from botTurn follow-up)
      if (currentSnap.phase === Phase.ExchangeDiscard && currentSnap.exchangeCards && !_pendingExchangeSlot) {
        deckEvents.push({ card: currentSnap.exchangeCards[0], direction: "out", description: "drawn" });
        deckEvents.push({ card: currentSnap.exchangeCards[1], direction: "out", description: "drawn" });
      }
      const hadPending = _pendingExchangeSlot != null;
      const humanAction = await humanTurn(
        game, currentSnap, buf, waitForKey, humanSeat, w, h, actionY, actionH, redraw, actionContextText, playerName, isHuman, history, _pendingExchangeSlot,
      );
      if (hadPending) _pendingExchangeSlot = null;
      // Track exchange returns
      if (currentSnap.phase === Phase.ExchangeDiscard && humanAction >= Action.DiscardSlot0 && humanAction <= Action.DiscardSlot3) {
        const slot = humanAction - Action.DiscardSlot0;
        const player = currentSnap.players[humanSeat];
        let discardedType: CardType | undefined;
        if (slot < 2 && player?.cards[slot]?.alive) {
          discardedType = player.cards[slot].type;
        } else if (currentSnap.exchangeCards) {
          const exIdx = slot - 2;
          if (exIdx >= 0 && exIdx < 2) discardedType = currentSnap.exchangeCards[exIdx];
        }
        if (discardedType != null) {
          deckEvents.push({ card: discardedType, direction: "in", description: "returned" });
        }
      }
      logAction(humanAction, humanSeat, currentSnap.phase);
      game.step(humanAction);
      // Log exchange completion
      if (currentSnap.phase === Phase.ExchangeDiscard) {
        const np = game.getSnapshot().phase;
        if (np !== Phase.ExchangeDiscard) {
          history.push(`You complete Exchange.`);
        }
      }
      redraw(game.getSnapshot());
      await doSleep(150);
    } else {
      // Another bot responds
      const respAgent = agents[currentActive];
      if (!respAgent) break;
      const prevPhase = currentSnap.phase;
      const obs2 = game.observe(currentActive);
      const mask2 = game.validMask;
      const action2 = await respAgent.chooseAction(obs2, mask2);
      logAction(action2, currentActive, currentSnap.phase);
      game.step(action2);
      // Log exchange completion for bots
      if (prevPhase === Phase.ExchangeDiscard) {
        const np = game.getSnapshot().phase;
        if (np !== Phase.ExchangeDiscard) {
          history.push(`${playerName(currentActive)} completes Exchange.`);
          deckEvents.push({ direction: "swap", description: `${playerName(currentActive)} exchanged` });
        }
      }
      redraw(game.getSnapshot());
      await doSleep(150);
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
  history: HistoryTicker,
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
    if (name === "h") {
      await showFullHistory(buf, waitForKey, history, w, h);
      renderGameOver(buf, result, w, h);
    }
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
    return { ...withTarget, type: "challenge" };
  if (action === Action.Pass)
    return { ...base, type: "pass" };
  if (action >= Action.BlockContessa && action <= Action.BlockDuke)
    return { ...base, type: "block", blockCard: blockCardType(action) ?? undefined };
  if (action >= Action.DiscardSlot0 && action <= Action.DiscardSlot3)
    return { ...base, type: "lose_card" };
  return null;
}
