import { assignment } from "./generated_expression.mjs";

// Memoize expensive, pure decisions with one changing input. Keep the original
// expression on misses; do not rewrite EEL remainder or approximate equality.
export function cachePointDecisions(lines, variables, capacity) {
  const sample = variables.get("sample");
  if (
    sample === undefined ||
    lines.some((line) => new RegExp(`v\\[${sample}\\]=`).test(line))
  )
    return lines;
  return lines.map((line) => {
    const match = assignment.exec(line);
    if (!match || variables.size + 2 > capacity) return line;
    const [, destination, expression] = match;
    if ((expression.match(/milk_mod\(/g) || []).length < 3) return line;
    const inputs = [
      ...new Set([...expression.matchAll(/v\[(\d+)\]/g)].map((m) => m[1])),
    ];
    if (inputs.length !== 1 || inputs[0] === destination) return line;
    // Accept only generated arithmetic, lazy decisions, and these pure helpers.
    // Unknown calls, writes, sequenced temporaries and random state are excluded.
    const rest = expression
      .replace(/v\[\d+\]/g, "")
      .replace(/\b(?:milk_mod|milk_equal|milk_truth)\(/g, "(")
      .replace(/\b\d+(?:\.\d*)?(?:e[+-]?\d+)?f?\b/gi, "");
    if (/[^\s()+*,?:.\-]/.test(rest) || /\+\+|--/.test(rest)) return line;
    const slots = [];
    for (let i = 0; i < 2; ++i) {
      const slot = variables.size;
      let name = `__point_decision_${slot}`;
      while (variables.has(name)) name += "_";
      variables.set(name, slot);
      slots.push(`v[${slot}]`);
    }
    const input = `v[${inputs[0]}]`;
    const [key, value] = slots;
    // Recompute at every wave boundary. Zero also misses so +0/-0 can never
    // alias; NaN naturally misses. Cached result survives destination writes.
    return `    if (v[${sample}] != 0 && ${input} != 0 && ${input} == ${key})
        v[${destination}]=${value};
    else
    {
${line}
        ${key}=${input};
        ${value}=v[${destination}];
    }`;
  });
}
