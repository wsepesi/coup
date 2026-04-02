// ── Theme colors (CSS variable names → values) ───────────────────

export const THEME = {
  bg: "#111111",
  border: "#555555",
  textDefault: "#DDDDDD",
  textDim: "#888888",
  textBright: "#FFFFFF",
  selectionBg: "#444455",
  you: "#00FFAA",
  dead: "#CC0000",
  coinGain: "#00DD00",
  coinLoss: "#FF5555",
  challengeWin: "#00FF00",
  challengeFail: "#FF5555",
  cursor: "#CCCCCC",
} as const;

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
  Duke: "\u2666\u2666\u2666",
  Assassin: "\u2020\u2020\u2020",
  Captain: "\u2693\u2693\u2693",
  Ambassador: "\u2726\u2726\u2726",
  Contessa: "\u2665\u2665\u2665",
};

export const ROLE_SHORT: Record<string, string> = {
  Duke: "Dk",
  Assassin: "As",
  Captain: "Cp",
  Ambassador: "Am",
  Contessa: "Ct",
};

export const HIDDEN_CARD = "\u2593\u2593";

// ── Influence indicators ──────────────────────────────────────────

export const INFLUENCE = {
  alive2: "\u25CF", // ●
  alive1: "\u25CB", // ○
  dead: "\u2620",   // ☠
} as const;

// ── Action space (matches the 32-action fixed layout) ─────────────

// Action categories for color coding
export function actionCategory(action: number): "general" | "aggressive" | "response" | "block" | "discard" {
  if (action <= 3) return "general";
  if (action <= 21) return "aggressive";
  if (action === 22) return "response";
  if (action === 23) return "response";
  if (action >= 24 && action <= 27) return "block";
  return "discard";
}
