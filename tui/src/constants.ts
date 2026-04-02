// T8: Constants & palette

import { CardType } from "@coup/game-client";

type ColorPalette = {
  bg: string;
  border: string;
  textDefault: string;
  textDim: string;
  textBright: string;
  you: string;
  enemy: string;
  bot: string;
  dead: string;
  coinGain: string;
  coinLoss: string;
  challengeWin: string;
  challengeFail: string;
  duke: string;
  assassin: string;
  captain: string;
  ambassador: string;
  contessa: string;
  cursor: string;
};

const COLORS_DARK: ColorPalette = {
  bg:            "#111111",
  border:        "#444444",
  textDefault:   "#CCCCCC",
  textDim:       "#666666",
  textBright:    "#FFFFFF",
  you:           "#00FFAA",
  enemy:         "#CCCCCC",
  bot:           "#888888",
  dead:          "#AA0000",
  coinGain:      "#00BB00",
  coinLoss:      "#FF4444",
  challengeWin:  "#00FF00",
  challengeFail: "#FF4444",
  duke:          "#AA00FF",
  assassin:      "#FF0000",
  captain:       "#0088FF",
  ambassador:    "#00CC44",
  contessa:      "#FF66AA",
  cursor:        "#FFFF00",
};

const COLORS_LIGHT: ColorPalette = {
  bg:            "#F5F5F0",
  border:        "#AAAAAA",
  textDefault:   "#222222",
  textDim:       "#888888",
  textBright:    "#000000",
  you:           "#007755",
  enemy:         "#333333",
  bot:           "#555555",
  dead:          "#CC0000",
  coinGain:      "#007700",
  coinLoss:      "#CC0000",
  challengeWin:  "#007700",
  challengeFail: "#CC0000",
  duke:          "#7700CC",
  assassin:      "#CC0000",
  captain:       "#0055CC",
  ambassador:    "#007733",
  contessa:      "#CC3377",
  cursor:        "#CC8800",
};

export let COLORS: ColorPalette = { ...COLORS_LIGHT };

export function setTheme(mode: "light" | "dark") {
  Object.assign(COLORS, mode === "dark" ? COLORS_DARK : COLORS_LIGHT);
}

export const CARD_ABBREV: Record<CardType, string> = {
  [CardType.Duke]:       "Dk",
  [CardType.Assassin]:   "As",
  [CardType.Captain]:    "Cp",
  [CardType.Ambassador]: "Am",
  [CardType.Contessa]:   "Ct",
};

export const CARD_SYMBOLS: Record<CardType, string> = {
  [CardType.Duke]:       "♦♦♦",
  [CardType.Assassin]:   "†††",
  [CardType.Captain]:    "⚓⚓⚓",
  [CardType.Ambassador]: "✦✦✦",
  [CardType.Contessa]:   "♥♥♥",
};

export function getCardColors(): Record<CardType, string> {
  return {
    [CardType.Duke]:       COLORS.duke,
    [CardType.Assassin]:   COLORS.assassin,
    [CardType.Captain]:    COLORS.captain,
    [CardType.Ambassador]: COLORS.ambassador,
    [CardType.Contessa]:   COLORS.contessa,
  };
}

export const MIN_TERM_WIDTH = 80;
export const MIN_TERM_HEIGHT = 24;

export const SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

export const ACTION_LABELS: Record<number, string> = {
  0: "Income",
  1: "Foreign Aid",
  2: "Tax",
  3: "Exchange",
  22: "Challenge",
  23: "Pass",
  24: "Block (Contessa)",
  25: "Block (Captain)",
  26: "Block (Ambassador)",
  27: "Block (Duke)",
};

export const ACTION_CATEGORY_LABELS: Record<string, string> = {
  coup: "Coup",
  steal: "Steal",
  assassinate: "Assassinate",
};
