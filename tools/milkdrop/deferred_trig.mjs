import { milkdrop } from "../contracts/load.mjs";
import { splitBinary, assignment } from "./generated_expression.mjs";

// Local def-use analysis over the compiler's generated straight-line point code.
// No preset identities or fixed slot numbers. Only pure trig temporaries whose
// every read belongs to a proven skippable additive term may be deferred.
function refs(text) {
  return [...text.matchAll(/v\[(\d+)\]/g)].map((m) => Number(m[1]));
}
function termSlots(text) {
  const slot = /^v\[(\d+)\]$/.exec(text);
  if (slot) return [Number(slot[1])];
  for (const op of ["+", "-"]) {
    const pair = splitBinary(text, op);
    if (pair) {
      const a = termSlots(pair[0]),
        b = termSlots(pair[1]);
      // At most four bounded trig operands: their sum/difference is finite.
      if (a && b && a.length + b.length <= 4) return [...a, ...b];
    }
  }
  return null;
}
export function deferInactiveTrig(lines, variables) {
  const sample = variables.get("sample");
  if (sample === undefined) return null;
  const outputs = new Set(
    ["x", "y", "r", "g", "b", "a", "sample", "value1", "value2"].map((n) =>
      variables.get(n),
    ),
  );
  const writes = new Map();
  for (const line of lines)
    for (const m of line.matchAll(/v\[(\d+)\]=/g))
      writes.set(Number(m[1]), (writes.get(Number(m[1])) || 0) + 1);
  const candidates = new Map(),
    consumers = new Map();
  lines.forEach((line, index) => {
    const a = assignment.exec(line);
    if (!a) return;
    const destination = Number(a[1]),
      rhs = a[2];
    const trig = /^(sinf|cosf)\((.*)\)$/.exec(rhs);
    if (
      trig &&
      writes.get(destination) === 1 &&
      !outputs.has(destination) &&
      !/[?=]/.test(trig[2]) &&
      !/milk_rand\(|\br\d+\b/.test(trig[2])
    )
      candidates.set(destination, {
        destination,
        index,
        trig: trig[1],
        angle: trig[2],
      });
    const op = splitBinary(rhs, "+") ? "+" : "-";
    const sum = splitBinary(rhs, op),
      product = sum && /^v\[\d+\]$/.test(sum[0]) && splitBinary(sum[1], "*");
    const slots = product && termSlots(product[1]);
    if (
      slots &&
      !/[=]/.test(rhs) &&
      !/milk_truth\(|milk_rand\(|\br\d+\b/.test(rhs)
    )
      consumers.set(index, {
        destination,
        base: sum[0],
        scale: product[0],
        term: product[1],
        slots,
        op,
      });
  });
  // Removing one candidate can invalidate a compound consumer, which in turn
  // invalidates other candidates. Iterate to a conservative fixed point.
  let changed;
  do {
    changed = false;
    for (const [index, c] of consumers) {
      if (
        c.slots.some((s) => !candidates.has(s)) ||
        refs(c.base + c.scale).some((s) => candidates.has(s))
      ) {
        consumers.delete(index);
        changed = true;
      }
    }
    for (const [slot, c] of candidates) {
      let used = false,
        valid = true;
      lines.forEach((line, index) => {
        const a = assignment.exec(line);
        const reads = refs(a ? a[2] : line);
        if (!reads.includes(slot)) return;
        used = true;
        if (index <= c.index || !consumers.get(index)?.slots.includes(slot))
          valid = false;
      });
      if (!used || !valid) {
        candidates.delete(slot);
        changed = true;
      }
    }
  } while (changed);
  if (!candidates.size) return null;
  const groups = [],
    bySlot = new Map();
  for (const c of candidates.values()) {
    if (bySlot.has(c.destination)) continue;
    const next = [...candidates.values()].find(
      (n) =>
        n.index === c.index + 1 && n.angle === c.angle && n.trig !== c.trig,
    );
    const group = {
      id: groups.length,
      members: next ? [c, next] : [c],
      angle: c.angle,
    };
    groups.push(group);
    for (const m of group.members) bySlot.set(m.destination, group);
  }
  function evaluate(g) {
    const angle = `defer_angle_${g.id}`;
    if (g.members.length === 1) {
      const m = g.members[0];
      return `v[${m.destination}]=milk_finite(${m.trig}(${angle}));`;
    }
    const sin = g.members.find((m) => m.trig === "sinf"),
      cos = g.members.find((m) => m.trig === "cosf");
    return `{ float ds, dc; milk_fast_sincos(${angle}, &ds, &dc); v[${sin.destination}]=milk_finite(ds); v[${cos.destination}]=milk_finite(dc); }`;
  }
  const result = [...lines];
  for (const g of groups) {
    result[g.members[0].index] = `    float defer_angle_${g.id}=${g.angle};
    int defer_ready_${g.id}=defer_restore || !(fabsf(defer_angle_${g.id}) <= ${milkdrop.MILKDROP_TRIG_FAST_LIMIT}.0f);
    if (defer_ready_${g.id}) { ${evaluate(g)} }`;
    if (g.members.length === 2) result[g.members[1].index] = "";
  }
  for (const [index, c] of consumers) {
    const used = [...new Set(c.slots.map((s) => bySlot.get(s)))];
    const bounds = used
      .map(
        (g) =>
          `fabsf(defer_angle_${g.id}) <= ${milkdrop.MILKDROP_TRIG_FAST_LIMIT}.0f`,
      )
      .join(" && ");
    const ensure = used
      .map(
        (g) =>
          `if (!defer_ready_${g.id}) { ${evaluate(g)} defer_ready_${g.id}=1; }`,
      )
      .join("\n        ");
    result[index] = `    { float defer_base=${c.base}, defer_scale=${c.scale};
      if (defer_scale == 0 && defer_base != 0 && isfinite(defer_base) && ${bounds})
        v[${c.destination}]=milk_finite(defer_base);
      else {
        ${ensure}
        v[${c.destination}]=milk_finite((defer_base${c.op}(defer_scale*${c.term})));
      } }`;
  }
  // The driver uses sample=0 for a single-point wave; materialize that too.
  result.unshift(
    `    const int defer_restore=!(v[${sample}] > 0 && v[${sample}] < 1);`,
  );
  return result;
}
