// ── Card / role definitions ───────────────────────────────────────

export const ROLE_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa"] as const;

export const ROLE_COLORS: Record<string, string> = {
  Duke: "#997700",
  Assassin: "#CC0000",
  Captain: "#0055CC",
  Ambassador: "#007733",
  Contessa: "#CC3377",
};

export const ROLE_SYMBOLS: Record<string, string> = {
  Duke: "♦♦♦",
  Assassin: "†††",
  Captain: "⚓⚓⚓",
  Ambassador: "✦✦✦",
  Contessa: "♥♥♥",
};

export const ROLE_SHORT: Record<string, string> = {
  Duke: "Dk",
  Assassin: "As",
  Captain: "Cp",
  Ambassador: "Am",
  Contessa: "Ct",
};

export const ROLE_ABILITY: Record<string, string> = {
  Duke: "Tax +3 coins; blocks Foreign Aid",
  Assassin: "Pay 3 to assassinate",
  Captain: "Steal 2 coins; blocks stealing",
  Ambassador: "Exchange with the deck; blocks stealing",
  Contessa: "Blocks assassination",
};

export const HIDDEN_CARD = "▓▓";

// ── Action space (matches the 32-action fixed layout) ─────────────

export const ACTION = {
  INCOME: 0,
  FOREIGN_AID: 1,
  TAX: 2,
  EXCHANGE: 3,
  COUP0: 4,
  STEAL0: 10,
  ASSASSINATE0: 16,
  CHALLENGE: 22,
  PASS: 23,
  BLOCK_CONTESSA: 24,
  BLOCK_CAPTAIN: 25,
  BLOCK_AMBASSADOR: 26,
  BLOCK_DUKE: 27,
  DISCARD0: 28,
} as const;

export function actionTarget(a: number): number | null {
  if (a >= 4 && a <= 9) return a - 4;
  if (a >= 10 && a <= 15) return a - 10;
  if (a >= 16 && a <= 21) return a - 16;
  return null;
}
