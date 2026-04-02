import { useEffect } from "react";

/**
 * Attaches a keydown listener to window with a key→callback map.
 * Skips when an input/textarea is focused (except Escape).
 */
export function useKeyboard(
  keyMap: Record<string, () => void>,
  deps: unknown[] = [],
  enabled: boolean = true,
) {
  useEffect(() => {
    if (!enabled) return;
    const handler = (e: KeyboardEvent) => {
      const tag = (document.activeElement as HTMLElement)?.tagName;
      if ((tag === "INPUT" || tag === "TEXTAREA") && e.key !== "Escape") return;

      const fn = keyMap[e.key];
      if (fn) {
        e.preventDefault();
        fn();
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [enabled, ...deps]);
}
