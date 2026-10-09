// Maximum angle supported by milk_fast_sin/cos without a libm fallback.
import { milkdrop } from "../contracts/load.mjs";

// Prove a nested pure arithmetic/trig expression finite without evaluating trig.
// Unknown syntax fails closed. Bounds include ample float-rounding headroom.
export function nestedTrigGuard(expression) {
  const guards = new Set();
  let trigCount = 0;
  function bound(text, inAngle = false) {
    if (/^v\[\d+\]$/.test(text)) {
      guards.add(`fabsf(${text}) <= ${milkdrop.MILKDROP_TRIG_FAST_LIMIT}.0f`);
      return milkdrop.MILKDROP_TRIG_FAST_LIMIT;
    }
    if (/^\d+(?:\.\d*)?(?:e[+-]?\d+)?f$/i.test(text)) {
      const n = Math.abs(Number(text.slice(0, -1)));
      return Number.isFinite(n) && n <= 1e30 ? n * 2 : null;
    }
    const trig = /^(sinf|cosf)\((.*)\)$/.exec(text);
    if (trig) {
      // Guard expressions must themselves remain cheap and free of trig calls.
      if (inAngle || bound(trig[2], true) === null) return null;
      guards.add(
        `fabsf(${trig[2]}) <= ${milkdrop.MILKDROP_TRIG_FAST_LIMIT}.0f`,
      );
      ++trigCount;
      return 2; // Existing bounded fast trig is safely within [-2, 2].
    }
    if (text[0] !== "(" || text.at(-1) !== ")") return null;
    let depth = 0;
    for (let i = 1; i < text.length - 1; ++i) {
      if (text[i] === "(") ++depth;
      else if (text[i] === ")") --depth;
      else if (
        !depth &&
        i > 1 &&
        "+-*".includes(text[i]) &&
        text[i - 1] !== "e" &&
        text[i - 1] !== "E"
      ) {
        const a = bound(text.slice(1, i), inAngle),
          b = bound(text.slice(i + 1, -1), inAngle);
        if (a === null || b === null) return null;
        const result = 2 * (text[i] === "*" ? a * b : a + b);
        return result <= 1e30 ? result : null;
      }
    }
    if (text[1] === "-") return bound(text.slice(2, -1), inAngle);
    return null;
  }
  return bound(expression) !== null && trigCount
    ? [...guards].join(" && ")
    : null;
}
