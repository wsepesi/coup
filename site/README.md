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
  components/         React components
  hooks/              Keyboard + history tracking hooks
  lib/                Protocol types, room WebSocket hook (ws.ts), identity/API (identity.ts)
```

## Stack

Next.js 15, React 19, Tailwind CSS, TypeScript.
