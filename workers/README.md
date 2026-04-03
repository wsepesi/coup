# Workers (Game Server)

Cloudflare Workers backend for multiplayer Coup. Manages lobbies, game rooms, and bot players via Durable Objects.

## Setup

```bash
bun install
```

## Development

```bash
bun run dev
```

Starts a local Wrangler dev server on `localhost:8787`.

## Deploy

```bash
bun run deploy
```

## Architecture

Uses two Durable Objects:

- **Matchmaker** -- lobby creation and player matchmaking
- **GameRoom** -- runs game state via WASM bridge to the C engine, manages WebSocket connections, drives bot players

## Key Files

```
src/
  index.ts          Entry point / router
  matchmaker.ts     Lobby management
  game-room.ts      Game state + WebSocket handling
  bot.ts            Bot player logic
  wasm-bridge.ts    C engine WASM interface
  types.ts          Shared types
```
