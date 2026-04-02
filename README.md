# Coup

Terminal-based Coup card game with AI opponents, built for reinforcement learning research.

## Quick Start

**Prerequisites:** [Bun](https://bun.sh) 1.0+ and a C compiler (`cc` — Clang on macOS, GCC on Linux).

```bash
bun install
bun run play
```

Or build the C engine explicitly and pass options:

```bash
./tui/build.sh --players 4 --difficulty hard
```

## CLI Options

| Flag | Description | Default |
|------|-------------|---------|
| `--players N` | Number of players (2-6) | Interactive setup |
| `--seat N` | Your seat position (0-based) | 0 |
| `--difficulty easy\|medium\|hard` | Bot difficulty | medium |
| `--seed N` | Random seed for reproducibility | random |
| `--fast` | Skip bot thinking animations | off |
| `--light` | Use light color theme | dark |

Without `--players`, an interactive setup screen lets you configure the game.

## Controls

| Key | Action |
|-----|--------|
| Arrow keys | Navigate menus / select targets |
| Enter | Confirm selection |
| 1-9 | Quick-select option by number |
| Space | Toggle card selection (Ambassador exchange) |
| H | Open full scrollable history |
| Escape | Go back / close history |
| F | Fast-forward (after elimination) |

## Game Rules

Coup is a bluffing card game for 2-6 players. Each player starts with 2 influence cards (hidden) and 2 coins. Last player standing wins.

**Roles:**

| Role | Action | Blocks |
|------|--------|--------|
| Duke | Tax: +3 coins | Blocks Foreign Aid |
| Assassin | Assassinate: pay 3, target loses influence | — |
| Captain | Steal: take 2 coins from target | Blocks Steal |
| Ambassador | Exchange: draw 2, keep best 2 | Blocks Steal |
| Contessa | — | Blocks Assassinate |

**General actions** (no role required): Income (+1 coin), Foreign Aid (+2 coins), Coup (pay 7, target loses influence — mandatory at 10+ coins).

**Bluffing:** You can claim any role regardless of your cards. Other players may **challenge** your claim — if you were bluffing, you lose influence; if truthful, the challenger loses influence. **Blocks** can also be challenged.

## Project Structure

```
c_engine/          Pure C game engine (32-byte state, ~1400 LOC)
cpp_engine/        OpenSpiel C++ engine (for MCCFR)
packages/
  game-client/     TypeScript FFI wrapper + bot agents
tui/               Terminal UI (OpenTUI framework)
pufferlib/         PufferLib RL environment binding
training/          PPO + MCCFR training scaffolding
```
