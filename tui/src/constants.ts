// T8: Constants & palette

import { CardType } from "@coup/game-client";

type ColorPalette = {
  bg: string;
  border: string;
  textDefault: string;
  textDim: string;
  textBright: string;
  selectionBg: string;
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
  border:        "#555555",
  textDefault:   "#DDDDDD",
  textDim:       "#888888",
  textBright:    "#FFFFFF",
  selectionBg:   "#444455",
  you:           "#00FFAA",
  enemy:         "#DDDDDD",
  bot:           "#AAAAAA",
  dead:          "#CC0000",
  coinGain:      "#00DD00",
  coinLoss:      "#FF5555",
  challengeWin:  "#00FF00",
  challengeFail: "#FF5555",
  duke:          "#FFB020",
  assassin:      "#FF3333",
  captain:       "#3399FF",
  ambassador:    "#00DD55",
  contessa:      "#FF77BB",
  cursor:        "#CCCCCC",
};

const COLORS_LIGHT: ColorPalette = {
  bg:            "#F5F5F0",
  border:        "#999999",
  textDefault:   "#222222",
  textDim:       "#777777",
  textBright:    "#000000",
  selectionBg:   "#A8A898",
  you:           "#007755",
  enemy:         "#333333",
  bot:           "#444444",
  dead:          "#CC0000",
  coinGain:      "#007700",
  coinLoss:      "#CC0000",
  challengeWin:  "#007700",
  challengeFail: "#CC0000",
  duke:          "#997700",
  assassin:      "#CC0000",
  captain:       "#0055CC",
  ambassador:    "#007733",
  contessa:      "#CC3377",
  cursor:        "#444444",
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
