import { cachePointDecisions } from "./point_decisions.mjs";
import { guardBooleanTrig } from "./boolean_trig.mjs";
import { reusePointTrig } from "./point_reuse.mjs";
import {
  splitBinary,
  assignment as generatedAssignment,
} from "./generated_expression.mjs";
import { nestedTrigGuard } from "./nested_trig.mjs";
import { deferInactiveTrig } from "./deferred_trig.mjs";
// Cache only pure point expressions whose dependencies are fixed for this loop.
// Deferred pure temporaries may remain stale internally; rendered outputs and
// complete end-of-wave state are preserved. All other assignments retain order.
export function cachePointInvariants(
  code,
  variables,
  capacity,
  enableDeferred = true,
) {
  const originalSize = variables.size;
  if (originalSize >= capacity) return code;
  const lines = code.split("\n");
  const assigned = new Set(
    [...code.matchAll(/v\[(\d+)\]=/g)].map((m) => Number(m[1])),
  );
  const varying = new Set(
    ["sample", "value1", "value2", "x", "y", "r", "g", "b", "a"].map((name) =>
      variables.get(name),
    ),
  );
  const known = new Map();
  for (let i = 0; i < originalSize; ++i)
    if (!assigned.has(i) && !varying.has(i)) known.set(i, `v[${i}]`);
  const setup = [],
    cache = new Map();
  function invariant(expression) {
    // Don't speculate lazy branches, random calls, or sequenced temporaries.
    if (/[?=]/.test(expression) || /milk_rand\(|\br\d+\b/.test(expression))
      return null;
    let valid = true;
    const result = expression.replace(/v\[(\d+)\]/g, (_, index) => {
      if (!known.has(Number(index))) valid = false;
      return known.get(Number(index)) || "";
    });
    return valid ? result : null;
  }
  function remember(expression) {
    if (cache.has(expression)) return cache.get(expression);
    if (variables.size >= capacity) return null;
    const index = variables.size;
    let name = `__point_cache_${index}`;
    while (variables.has(name)) name += "_";
    variables.set(name, index);
    const slot = `v[${index}]`;
    setup.push(`        ${slot}=${expression};`);
    cache.set(expression, slot);
    // Cached slots themselves are immutable throughout the point loop.
    known.set(index, slot);
    return slot;
  }
  const hoisted = lines.map((line) => {
    const assignment = generatedAssignment.exec(line);
    if (!assignment) return line;
    const destination = Number(assignment[1]);
    let rhs = assignment[2];
    const fixed = invariant(rhs);
    if (fixed !== null) {
      const slot = remember(`milk_finite(${fixed})`);
      if (slot) {
        known.set(destination, slot);
        return `    (void)(v[${destination}]=${slot});`;
      }
    }
    // Extract balanced pure calls; no algebraic rewriting or approximations.
    // Parser emits lazy if through milk_truth. Other ternaries only select
    // literal boolean values, so calls outside them are always evaluated.
    if (!rhs.includes("milk_truth(")) {
      const pattern =
        /\b(sinf|cosf|milk_pow|milk_equal|milk_sign|milk_div|fabsf|fminf|fmaxf)\(/g;
      let match;
      while ((match = pattern.exec(rhs))) {
        let end = pattern.lastIndex,
          depth = 1;
        while (end < rhs.length && depth) {
          if (rhs[end] === "(") ++depth;
          if (rhs[end] === ")") --depth;
          ++end;
        }
        const expression = invariant(rhs.slice(match.index, end));
        const slot = expression === null ? null : remember(expression);
        if (slot) {
          rhs = rhs.slice(0, match.index) + slot + rhs.slice(end);
          pattern.lastIndex = match.index + slot.length;
        }
      }
    }
    // Hoist invariant arithmetic subexpressions too, without reassociating
    // operations. Do not remove a function call's argument parentheses.
    if (!rhs.includes("milk_truth(")) {
      function arithmetic(text) {
        let result = "";
        for (let i = 0; i < text.length;) {
          if (text[i] !== "(") {
            result += text[i++];
            continue;
          }
          let end = i + 1,
            depth = 1;
          while (end < text.length && depth) {
            if (text[end] === "(") ++depth;
            if (text[end] === ")") --depth;
            ++end;
          }
          const group = text.slice(i, end);
          const argument = i > 0 && /[a-zA-Z0-9_]/.test(text[i - 1]);
          const fixed =
            !argument && /v\[/.test(group) && /[+*/-]/.test(group)
              ? invariant(group)
              : null;
          const slot = fixed === null ? null : remember(fixed);
          result += slot || "(" + arithmetic(group.slice(1, -1)) + ")";
          i = end;
        }
        return result;
      }
      rhs = arithmetic(rhs);
    }
    known.delete(destination);
    return `    (void)(v[${destination}]=milk_finite(${rhs}));`;
  });
  // Reuse point-local results after loop invariants have been removed, before
  // guarded/deferred rewrites introduce conditional control flow.
  const gates = new Map();
  const output = reusePointTrig(hoisted.join("\n"))
    .split("\n")
    .map((line, index) => {
      const assignment = generatedAssignment.exec(line);
      if (!assignment) return line;
      const destination = Number(assignment[1]);
      const rhs = assignment[2];
      // Exact for a finite, nonzero base: adding either signed zero changes nothing.
      const operator = splitBinary(rhs, "+") ? "+" : "-";
      const sum = splitBinary(rhs, operator);
      const product =
        sum && /^v\[\d+\]$/.test(sum[0]) && splitBinary(sum[1], "*");
      const trig = product && /^(sinf|cosf)\((.*)\)$/.exec(product[1]);
      if (
        trig &&
        !/[=]/.test(rhs) &&
        !/milk_truth\(|milk_rand\(|\br\d+\b/.test(rhs)
      ) {
        gates.set(index, {
          destination,
          base: sum[0],
          scale: product[0],
          angle: trig[2],
          trig: trig[1],
          operator,
        });
        return `    { float gate_base=${sum[0]}, gate_scale=${product[0]}, gate_angle=${trig[2]};
    (void)(v[${destination}]=milk_finite((gate_scale == 0 && gate_base != 0 && isfinite(gate_base) && isfinite(gate_angle)) ? gate_base : (gate_base${operator}(gate_scale*${trig[1]}(gate_angle))))); }`;
      }
      // Nested terms need a finiteness proof: zero times infinity/NaN is not zero.
      const nested =
        product &&
        !trig &&
        !/[=]/.test(rhs) &&
        !/milk_truth\(|milk_rand\(|\br\d+\b/.test(rhs) &&
        nestedTrigGuard(product[1]);
      if (nested)
        return `    { float nested_base=${sum[0]}, nested_scale=${product[0]};
      if (nested_scale == 0 && nested_base != 0 && isfinite(nested_base) && ${nested})
        v[${destination}]=milk_finite(nested_base);
      else v[${destination}]=milk_finite((nested_base${operator}(nested_scale*${product[1]}))); }`;
      return `    (void)(v[${destination}]=milk_finite(${rhs}));`;
    });
  // Adjacent guarded terms can share angle evaluation and range reduction.
  // The second term must not observe the first destination; no state/RNG
  // expressions reach this pass because gate formation already excludes them.
  for (let i = 0; i + 1 < output.length; ++i) {
    const a = gates.get(i),
      b = gates.get(i + 1);
    if (
      !a ||
      !b ||
      a.trig === b.trig ||
      a.angle !== b.angle ||
      [...(b.base + b.scale + b.angle).matchAll(/v\[(\d+)\]/g)].some(
        (m) => Number(m[1]) === a.destination,
      )
    )
      continue;
    const valueA = a.trig === "sinf" ? "pair_s" : "pair_c";
    const valueB = b.trig === "sinf" ? "pair_s" : "pair_c";
    output[i] =
      `    { float base_a=${a.base}, scale_a=${a.scale}, base_b=${b.base}, scale_b=${b.scale}, angle=${a.angle};
      int skip_a=scale_a == 0 && base_a != 0 && isfinite(base_a) && isfinite(angle);
      int skip_b=scale_b == 0 && base_b != 0 && isfinite(base_b) && isfinite(angle);
      float pair_s=0, pair_c=0;
      if (!skip_a && !skip_b) milk_fast_sincos(angle, &pair_s, &pair_c);
      else if (!skip_a) ${valueA}=${a.trig}(angle);
      else if (!skip_b) ${valueB}=${b.trig}(angle);
      v[${a.destination}]=milk_finite(skip_a ? base_a : (base_a${a.operator}(scale_a*${valueA})));
      v[${b.destination}]=milk_finite(skip_b ? base_b : (base_b${b.operator}(scale_b*${valueB}))); }`;
    output[i + 1] = "";
    ++i;
  }
  const guarded = guardBooleanTrig(output, variables, capacity);
  const deferred = enableDeferred
    ? deferInactiveTrig(guarded, variables)
    : null;
  // Pair adjacent pure trig assignments only. If the first destination feeds
  // the angle, the second call would observe a different value: leave it alone.
  for (const statements of [deferred || guarded]) {
    for (let i = 0; i + 1 < statements.length; ++i) {
      const pattern =
        /^    \(void\)\(v\[(\d+)\]=milk_finite\((sinf|cosf)\((.*)\)\)\);$/;
      const a = pattern.exec(statements[i]),
        b = pattern.exec(statements[i + 1]);
      if (
        !a ||
        !b ||
        a[2] === b[2] ||
        a[3] !== b[3] ||
        /[?=]/.test(a[3]) ||
        /milk_rand\(|\br\d+\b/.test(a[3]) ||
        [...a[3].matchAll(/v\[(\d+)\]/g)].some((m) => m[1] === a[1])
      )
        continue;
      statements[i] = `      { float pair_s, pair_c;
        milk_fast_sincos(${a[3]}, &pair_s, &pair_c);
        v[${a[1]}]=milk_finite(${a[2] === "sinf" ? "pair_s" : "pair_c"});
        v[${b[1]}]=milk_finite(${b[2] === "sinf" ? "pair_s" : "pair_c"}); }`;
      statements[i + 1] = "";
      ++i;
    }
  }
  const body = cachePointDecisions(
    deferred || guarded,
    variables,
    capacity,
  ).join("\n");
  if (!setup.length) return body;
  // wave_step supplies sample=0 only at the first point, even for one-point waves.
  return `    if (v[${variables.get("sample")}] == 0)\n    {\n${setup.join("\n")}\n    }\n${body}`;
}
