// T7: Layout geometry — player positions in oval arrangement

export interface PlayerPosition {
  x: number;
  y: number;
  seat: number;
}

// Compute player positions for N players on an oval layout.
// Seat 0 (you) is always at bottom center. Others wrap clockwise.
// Returns positions in character coordinates for a terminal of given size.
export function computePlayerPositions(
  playerCount: number,
  termWidth: number,
  termHeight: number,
  tableHeight: number, // height of the table zone
): PlayerPosition[] {
  const positions: PlayerPosition[] = [];
  const cx = Math.floor(termWidth / 2);
  const cy = Math.floor(tableHeight / 2);
  const rx = Math.floor(termWidth * 0.35); // horizontal radius
  const ry = Math.floor(tableHeight * 0.35); // vertical radius

  // Predefined angle layouts per player count
  // Seat 0 at bottom (angle = PI/2 from top, or 270° in standard, we use radians from top going clockwise)
  // We place seat 0 at the bottom, then go clockwise
  const angles = getSeatAngles(playerCount);

  for (let i = 0; i < playerCount; i++) {
    const angle = angles[i];
    const x = Math.round(cx + rx * Math.sin(angle));
    const y = Math.round(cy + ry * Math.cos(angle));
    positions.push({ x, y, seat: i });
  }

  return positions;
}

function getSeatAngles(n: number): number[] {
  // Seat 0 at bottom (angle 0 = bottom), going clockwise
  // bottom = 0, right = PI/2, top = PI, left = 3PI/2
  switch (n) {
    case 2:
      return [0, Math.PI]; // you bottom, opponent top
    case 3:
      return [0, (Math.PI * 4) / 3, (Math.PI * 2) / 3]; // bottom, upper-left, upper-right
    case 4:
      return [0, (Math.PI * 5) / 4, Math.PI, (Math.PI * 3) / 4]; // bottom, lower-left, top, upper-right...
      // Actually: bottom, left, top, right
    case 5:
      return [0, (Math.PI * 6) / 5, (Math.PI * 4) / 5, (Math.PI * 2) / 5, (Math.PI * 8) / 5].map(a => a); // nope
    case 6:
      // Seat layout from plan: [0]=bottom, [1]=lower-left, [2]=top, [3]=upper-right, [4]=lower-right, [5]=lower-left
      // Actually plan says: [2] top, [1] upper-left, [3] upper-right, [5] lower-left, [4] lower-right, [0] bottom
      return [0, 1, 2, 3, 4, 5].map(i => (Math.PI * 2 * i) / 6);
    default:
      return Array.from({ length: n }, (_, i) => (Math.PI * 2 * i) / n);
  }
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
  // Filter to valid targets (alive, not self/human)
  const candidates = positions.filter(
    (p) => p.seat !== humanSeat && aliveMask[p.seat] && p.seat !== currentSeat
  );
  if (candidates.length === 0) return null;

  const current = positions.find((p) => p.seat === currentSeat);
  if (!current) return candidates[0].seat;

  // Find closest candidate in the given direction
  let best: PlayerPosition | null = null;
  let bestDist = Infinity;

  for (const c of candidates) {
    const dx = c.x - current.x;
    const dy = c.y - current.y; // positive = down in terminal coords

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

  // If no candidate in that direction, wrap around
  if (!best) {
    // Find the farthest candidate in the opposite direction (wrap)
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
export const PLAYER_DISPLAY_HEIGHT = 3;
export const HAND_DISPLAY_WIDTH = 24;
export const HAND_DISPLAY_HEIGHT = 5;
export const CENTER_BOX_WIDTH = 27;
export const CENTER_BOX_HEIGHT = 7;
