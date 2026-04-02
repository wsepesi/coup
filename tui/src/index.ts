// T26: CLI entry point

import { CliRenderer, resolveRenderLib, RGBA } from "@opentui/core";
import { FrameBufferRenderable } from "@opentui/core";
import type { KeyEvent } from "@opentui/core";
import { SetupScreen, showRulesScreen, type GameConfig } from "./setup.js";
import { runGame } from "./game-loop.js";
import { setTheme, MIN_TERM_WIDTH, MIN_TERM_HEIGHT } from "./constants.js";

async function main() {
  const args = parseArgs(process.argv.slice(2));

  if (args.dark) setTheme("dark");
  else setTheme("light");

  const cols = process.stdout.columns || 100;
  const rows = process.stdout.rows || 30;

  const lib = resolveRenderLib();
  const rendererPtr = lib.createRenderer(cols, rows, { testing: false });
  if (!rendererPtr) {
    console.error("Failed to create renderer");
    process.exit(1);
  }

  const renderer = new CliRenderer(
    lib,
    rendererPtr,
    process.stdin,
    process.stdout,
    cols,
    rows,
    {
      exitOnCtrlC: true,
      screenMode: "alternate-screen",
    },
  );

  // Create a FrameBuffer that fills the screen
  const fb = new FrameBufferRenderable(renderer, {
    width: cols,
    height: rows,
  });
  renderer.root.add(fb);
  fb.focusable = true;
  fb.focus();

  // Key event resolver — game loop awaits keys through this
  let keyResolver: ((key: KeyEvent) => void) | null = null;

  fb.onKeyDown = (key: KeyEvent) => {
    if (keyResolver) {
      const resolve = keyResolver;
      keyResolver = null;
      resolve(key);
    }
  };

  function waitForKey(): Promise<KeyEvent> {
    return new Promise<KeyEvent>((resolve) => {
      keyResolver = resolve;
    });
  }

  renderer.start();

  try {
    let config: GameConfig;

    if (args.players != null) {
      config = {
        players: args.players,
        seat: args.seat ?? 0,
        difficulty: args.difficulty ?? "medium",
        seed: args.seed ?? "",
      };
    } else {
      config = await showSetup(fb, waitForKey, cols, rows);
    }

    let choice: "replay" | "new" | "quit" = "replay";
    while (choice !== "quit") {
      if (choice === "new") {
        config = await showSetup(fb, waitForKey, cols, rows);
      }
      choice = await runGame(fb, waitForKey, config, cols, rows, args.fast ?? false);
    }
  } catch (err) {
    // Exit alternate screen so the error message is visible in the normal terminal
    renderer.stop();
    console.error("\n\x1b[31m=== Coup TUI Crashed ===\x1b[0m\n");
    console.error(err);
    console.error("\nPlease report this bug with the seed and steps to reproduce.");
    process.exit(1);
  } finally {
    process.exit(0);
  }
}

async function showSetup(
  fb: FrameBufferRenderable,
  waitForKey: () => Promise<KeyEvent>,
  width: number,
  height: number,
): Promise<GameConfig> {
  const setup = new SetupScreen();
  const buf = fb.frameBuffer;

  function render() {
    setup.render(buf, width, height);
  }

  render();

  while (true) {
    const key = await waitForKey();
    const result = setup.handleKey(key.name);
    if (result === "start") {
      return setup.getConfig();
    }
    if (result === "rules") {
      await showRulesScreen(buf, waitForKey, width, height);
      render();
      continue;
    }
    render();
  }
}

interface CliArgs {
  players?: number;
  seat?: number;
  difficulty?: "easy" | "medium" | "hard";
  seed?: string;
  fast?: boolean;
  dark?: boolean;
}

function parseArgs(argv: string[]): CliArgs {
  const args: CliArgs = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const next = argv[i + 1];
    switch (arg) {
      case "--players": args.players = parseInt(next); i++; break;
      case "--seat": args.seat = parseInt(next); i++; break;
      case "--difficulty": args.difficulty = next as any; i++; break;
      case "--seed": args.seed = next; i++; break;
      case "--fast": args.fast = true; break;
      case "--dark": args.dark = true; break;
    }
  }
  return args;
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
