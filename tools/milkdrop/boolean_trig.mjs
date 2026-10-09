import { assignment, splitBinary } from "./generated_expression.mjs";
import { milkdrop } from "../contracts/load.mjs";

// Recognize ((Boolean * trig(angle)) * scale) * sign without reassociating it.
// The accepted Boolean is a compiler-emitted comparison, not a lazy user branch.
function term(line) {
  const match = assignment.exec(line);
  if (!match) return null;
  const rhs = match[2];
  const sum = splitBinary(rhs, "+");
  const atom = /^(?:v\[\d+\]|-?\d+(?:\.\d*)?(?:e[+-]?\d+)?f)$/i;
  if (!sum || !atom.test(sum[0])) return null;
  const factors = [];
  let product = splitBinary(sum[1], "*");
  while (product && atom.test(product[1])) {
    factors.push(product[1]);
    product = splitBinary(product[0], "*");
  }
  if (!product) return null;
  const comparison =
    /^\(\(v\[\d+\]\)(?:>|<)\(-?\d+(?:\.\d*)?(?:e[+-]?\d+)?f\)\?1\.0f:0\.0f\)$/i;
  const trig = /^(sinf|cosf)\((v\[\d+\])\)$/.exec(product[1]);
  if (!trig || !comparison.test(product[0])) return null;
  return {
    destination: match[1],
    rhs,
    base: sum[0],
    enabled: product[0],
    call: product[1],
    trig: trig[1],
    angle: trig[2],
    factors,
  };
}

function skip(t) {
  return (
    `${t.enabled} == 0 && ${t.base} != 0 && isfinite(${t.base}) && fabsf(${t.angle}) <= ${milkdrop.MILKDROP_TRIG_FAST_LIMIT}.0f` +
    t.factors.map((f) => ` && isfinite(${f})`).join("")
  );
}

// Original state writes and multiplication order stay intact. Proven first-point
// angles may use extra compiler-owned slots, refreshed after their wave update.
export function guardBooleanTrig(lines, variables, capacity) {
  const sample = variables?.get("sample");
  const reset = new Set(
    ["x", "y", "r", "g", "b", "a", "sample", "value1", "value2"].map((n) =>
      variables?.get(n),
    ),
  );
  const writes = new Map();
  for (const line of lines)
    for (const m of line.matchAll(/v\[(\d+)\]\s*=/g)) {
      const slot = Number(m[1]);
      writes.set(slot, (writes.get(slot) || 0) + 1);
    }
  const firstOnly = new Map();
  if (sample !== undefined && !writes.has(sample))
    lines.forEach((line, index) => {
      const m = assignment.exec(line);
      if (!m) return;
      const slot = Number(m[1]);
      const prefix = `(milk_truth(((v[${sample}])>(0.000000000e+0f)?1.0f:0.0f))?(v[${slot}]):(`;
      if (!reset.has(slot) && writes.get(slot) === 1 && m[2].startsWith(prefix))
        firstOnly.set(slot, index);
    });
  const waveCache = new Map();
  function cachedPair(angle, index) {
    const slot = Number(angle.slice(2, -1));
    if (!firstOnly.has(slot) || firstOnly.get(slot) >= index) return null;
    if (waveCache.has(slot))
      return { ...waveCache.get(slot), initialize: false };
    if (variables.size + 2 > capacity) return null;
    const result = {
      sine: variables.size,
      cosine: variables.size + 1,
      initialize: true,
    };
    for (const index of [result.sine, result.cosine]) {
      let name = `__wave_trig_${index}`;
      while (variables.has(name)) name += "_";
      variables.set(name, index);
    }
    waveCache.set(slot, result);
    return result;
  }
  const output = [...lines];
  for (let i = 0; i < lines.length; ++i) {
    const a = term(lines[i]);
    if (!a) continue;
    const b = term(lines[i + 1] || "");
    if (
      b &&
      a.trig !== b.trig &&
      a.angle === b.angle &&
      a.enabled === b.enabled &&
      ![...b.rhs.matchAll(/v\[(\d+)\]/g)].some((m) => m[1] === a.destination)
    ) {
      const va = a.trig === "sinf" ? "bool_s" : "bool_c";
      const vb = b.trig === "sinf" ? "bool_s" : "bool_c";
      // A sole first-point update leaves a finite angle unchanged for the rest
      // of the wave. Initialize after that update, never before it. Fresh sample=0
      // resets the cache every wave; retain the exact original term arithmetic.
      const cached = cachedPair(a.angle, i);
      if (cached) {
        const sa = `v[${a.trig === "sinf" ? cached.sine : cached.cosine}]`;
        const sb = `v[${b.trig === "sinf" ? cached.sine : cached.cosine}]`;
        output[i] =
          (cached.initialize
            ? `    if (v[${sample}] == 0) milk_fast_sincos(${a.angle}, &v[${cached.sine}], &v[${cached.cosine}]);\n`
            : "") +
          `    v[${a.destination}] = milk_finite(${a.rhs.replace(a.call, sa)});\n    v[${b.destination}] = milk_finite(${b.rhs.replace(b.call, sb)});`;
        output[++i] = "";
        continue;
      }
      output[i] = `    { int skip_a = ${skip(a)}, skip_b = ${skip(b)};
      float bool_s = 0, bool_c = 0;
      if (!skip_a && !skip_b) milk_fast_sincos(${a.angle}, &bool_s, &bool_c);
      else if (!skip_a) ${va} = ${a.call};
      else if (!skip_b) ${vb} = ${b.call};
      v[${a.destination}] = milk_finite(skip_a ? ${a.base} : ${a.rhs.replace(a.call, va)});
      v[${b.destination}] = milk_finite(skip_b ? ${b.base} : ${b.rhs.replace(b.call, vb)}); }`;
      output[++i] = "";
    } else {
      output[i] =
        `    { v[${a.destination}] = milk_finite((${skip(a)}) ? ${a.base} : ${a.rhs}); }`;
    }
  }
  return output;
}
