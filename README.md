# Coup

Coup (the card game) implemented as a multi-agent RL environment with two independent game engines. The C engine targets [PufferLib](https://github.com/PufferAI/PufferLib) for PPO training, the C++ engine targets [OpenSpiel](https://github.com/google-deepmind/open_spiel) for CFR/game-theoretic analysis. A web frontend and terminal UI let you play against AI opponents.

## Play Online

The web version runs in the browser with multiplayer lobbies and configurable bot opponents.

**Prerequisites:** [Bun](https://bun.sh) 1.0+

```bash
# Start the game server
cd workers && bun install && bun run dev

# In another terminal, start the web app
cd site && bun install && bun run dev
```

Then open `http://localhost:3000`.

## Play in Terminal

**Prerequisites:** [Bun](https://bun.sh) 1.0+ and a C compiler (`cc`).

```bash
bun install
bun run play
```

Or with options:

```bash
bun run play -- --players 4 --difficulty hard --fast
```

| Flag | Description | Default |
|------|-------------|---------|
| `--players N` | Number of players (2-6) | Interactive setup |
| `--seat N` | Your seat position (0-based) | 0 |
| `--difficulty easy\|medium\|hard` | Bot difficulty | medium |
| `--seed N` | Random seed for reproducibility | random |
| `--fast` | Skip bot thinking animations | off |
| `--light` | Use light color theme | dark |

## Build & Test

```bash
make test          # Run all tests (C engine + cross-framework)
make test-c        # C engine core + PRNG tests
make test-cpp      # C++ engine via CMake (requires OpenSpiel submodule)
make test-cross    # Cross-framework Python tests (uv run)
make build-puffer  # Build PufferLib C extension
make test-puffer   # PufferLib perf benchmark
```

## Game Rules

Coup is a bluffing card game for 2-6 players. Each player starts with 2 influence cards (hidden) and 2 coins. Last player standing wins.

**Roles:**

| Role | Action | Blocks |
|------|--------|--------|
| Duke | Tax: +3 coins | Blocks Foreign Aid |
| Assassin | Assassinate: pay 3, target loses influence | -- |
| Captain | Steal: take 2 coins from target | Blocks Steal |
| Ambassador | Exchange: draw 2, keep best 2 | Blocks Steal |
| Contessa | -- | Blocks Assassinate |

**General actions** (no role required): Income (+1 coin), Foreign Aid (+2 coins), Coup (pay 7, target loses influence -- mandatory at 10+ coins).

**Bluffing:** You can claim any role regardless of your cards. Other players may **challenge** your claim -- if you were bluffing, you lose influence; if truthful, the challenger loses influence. **Blocks** can also be challenged.

## Project Structure

```
c_engine/          Pure C game engine (bit-packed ~32-byte state)
cpp_engine/        OpenSpiel C++ engine (for MCCFR/CFR)
site/              Next.js web frontend
workers/           Cloudflare Workers game server (WebSocket + Durable Objects)
wasm/              WASM build of C engine (for workers/browser)
tui/               Terminal UI (@opentui/core)
packages/
  game-client/     TypeScript FFI wrapper + bot agents
pufferlib/         PufferLib RL environment binding
training/          PPO + MCCFR training scaffolding
profiling/         Engine benchmarks (~3M games/sec on M4)
tests/             Cross-framework parity tests
```

## Architecture

Two fully independent engine implementations sharing the same rules and **fixed 32-action space** (0=income, 1=foreign_aid, 2=tax, 3=exchange, 4-9=coup targets, 10-15=steal targets, 16-21=assassinate targets, 22=challenge, 23=pass, 24-27=block variants, 28-31=discard slots). Cross-framework tests enforce parity.

The web stack compiles the C engine to WASM and runs it inside Cloudflare Workers Durable Objects, with a Next.js frontend connecting over WebSocket.

See each subdirectory's README for setup and details.
