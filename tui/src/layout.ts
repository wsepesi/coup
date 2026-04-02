// T7: Layout geometry — player positions in oval arrangement

export interface PlayerPosition {
  x: number;
  y: number;
  seat: number;
}

// Compute opponent positions on the upper arc of an oval.
// humanSeat is excluded — it's rendered separately as the hand at the bottom.
// Returns positions in character coordinates for the table zone (0-based, caller offsets).
export function computePlayerPositions(
  playerCount: number,
  termWidth: number,
  tableZoneHeight: number,
  humanSeat: number,
): PlayerPosition[] {
  const positions: PlayerPosition[] = [];
  const cx = Math.floor(termWidth / 2);
  const cy = Math.floor(tableZoneHeight / 2);
  // Radii: clamp for large screens, floor for tiny ones
  const rx = Math.min(Math.max(Math.floor(termWidth * 0.35), 15), 60);
  const ry = Math.min(Math.max(Math.floor(tableZoneHeight * 0.35), 2), 12);

  // Collect opponent seats
  const opponents: number[] = [];
  for (let i = 0; i < playerCount; i++) {
    if (i !== humanSeat) opponents.push(i);
  }

  // Spread opponents across the upper arc only (hand occupies the bottom).
  // Angle convention: 0 = bottom (y=cy+ry), PI = top (y=cy-ry).
  // Arc from 2PI/3 (upper-right) through PI (top) to 4PI/3 (upper-left).
  // This keeps all opponents in the upper half of the table zone.
  const arcStart = (2 * Math.PI) / 3;
  const arcEnd = (4 * Math.PI) / 3;
  const arcSpan = arcEnd - arcStart;

  for (let i = 0; i < opponents.length; i++) {
    let angle: number;
    if (opponents.length === 1) {
      angle = Math.PI; // single opponent at top center
    } else {
      angle = arcStart + i * arcSpan / (opponents.length - 1);
    }
    const x = Math.round(cx + rx * Math.sin(angle));
    const y = Math.round(cy + ry * Math.cos(angle));
    // Clamp y to stay within table zone with room for 2-line player display
    const clampedY = Math.max(0, Math.min(tableZoneHeight - 2, y));
    positions.push({ x, y: clampedY, seat: opponents[i] });
  }

  return positions;
}

// Spatial navigation: given current seat, direction, alive mask, return next seat
export type Direction = "up" | "down" | "left" | "right";

export function spatialNavigate(
  currentSeat: number,
  direction: Direction,
  aliveMask: boolean[],
  positions: PlayerPosition[],
  humanSeat: number,
): number | null {
  const candidates = positions.filter(
    (p) => p.seat !== humanSeat && aliveMask[p.seat] && p.seat !== currentSeat
  );
  if (candidates.length === 0) return null;

  const current = positions.find((p) => p.seat === currentSeat);
  if (!current) return candidates[0].seat;

  let best: PlayerPosition | null = null;
  let bestDist = Infinity;

  for (const c of candidates) {
    const dx = c.x - current.x;
    const dy = c.y - current.y;

    let inDirection = false;
    switch (direction) {
      case "up":    inDirection = dy < -1; break;
      case "down":  inDirection = dy > 1; break;
      case "left":  inDirection = dx < -1; break;
      case "right": inDirection = dx > 1; break;
    }

    if (inDirection) {
      const dist = Math.abs(dx) + Math.abs(dy);
      if (dist < bestDist) {
        bestDist = dist;
        best = c;
      }
    }
  }

  if (!best) {
    let farthest: PlayerPosition | null = null;
    let farthestDist = -1;
    for (const c of candidates) {
      const dist = Math.abs(c.x - current.x) + Math.abs(c.y - current.y);
      if (dist > farthestDist) {
        farthestDist = dist;
        farthest = c;
      }
    }
    return farthest?.seat ?? null;
  }

  return best.seat;
}

// Center of the table (for the action display box)
export function getTableCenter(termWidth: number, tableHeight: number) {
  return {
    x: Math.floor(termWidth / 2),
    y: Math.floor(tableHeight / 2),
  };
}

// Player card display width
export const PLAYER_DISPLAY_WIDTH = 22;
export const PLAYER_DISPLAY_HEIGHT = 4;
export const HAND_DISPLAY_WIDTH = 24;
export const HAND_DISPLAY_HEIGHT = 5;
export const CENTER_BOX_WIDTH = 38;
export const CENTER_BOX_HEIGHT = 7;
