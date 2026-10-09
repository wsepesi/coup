# Site (Web Frontend)

Next.js 15 web app for playing Coup in the browser. Connects to the Cloudflare Workers backend via WebSocket.

## Setup

```bash
bun install
```

Create `.env.local`:
```
NEXT_PUBLIC_WS_URL=ws://localhost:8787
```

## Development

```bash
bun run dev
```

Opens at `http://localhost:3000`. Requires the workers backend running locally (see `../workers/`).

## Key Structure

```
src/
  app/
    page.tsx          Landing page
    lobby/[code]/     Room page: lobby -> game -> results -> rematch (one WebSocket)
    game/[code]/      Redirects to /lobby/[code]
  components/         React components (GameBoard = Table + HistoryPanel + action pickers)
  hooks/              Keyboard hook
  lib/                Protocol types, room WebSocket hook (ws.ts), identity/API (identity.ts)
```

## Game screen principle

Play like in person, with perfect recall. The table (`Table.tsx`) shows only what you'd see sitting there: coins, face-down cards, face-up lost cards, what each player said this turn, your own hand, the deck. The history (`HistoryPanel.tsx`) is the complete written record, as a transcript (**everything**) or one column per player (**swimlanes**); `H` switches. Don't add anything derived from the history (odds, card counting, claim tracking, threat labels): players do that reasoning themselves.

## Stack

Next.js 15, React 19, Tailwind CSS, TypeScript.
