// Reuse identical trig calls within straight-line point code. Never speculate
// conditional calls or carry cached values across unknown writes/control flow.
export function reusePointTrig(code) {
  const lines = code.split("\n");
  const cache = new Map();
  const groups = [];
  for (let line = 0; line < lines.length; ++line) {
    if (!lines[line].trim()) continue;
    const assignment = /^    \(void\)\(v\[(\d+)\]=(.*)\);$/.exec(lines[line]);
    if (
      !assignment ||
      /[?=&|]/.test(assignment[2]) ||
      /milk_rand\(|\br\d+\b/.test(assignment[2])
    ) {
      cache.clear();
      continue;
    }
    const calls = /\b(sinf|cosf)\(/g;
    let call;
    while ((call = calls.exec(lines[line]))) {
      const begin = call.index;
      const argumentBegin = calls.lastIndex;
      let end = argumentBegin,
        depth = 1;
      for (; end < lines[line].length && depth; ++end) {
        if (lines[line][end] === "(") ++depth;
        else if (lines[line][end] === ")") --depth;
      }
      const argument = lines[line].slice(argumentBegin, end - 1);
      // Restrict arguments to slot reads, numeric literals and arithmetic.
      const rest = argument
        .replace(/v\[\d+\]/g, "")
        .replace(/\d+(?:\.\d*)?(?:e[+-]?\d+)?f?/gi, "");
      if (depth || /[^\s()+*/.\-]/.test(rest)) continue;
      let group = cache.get(argument);
      if (!group) {
        group = {
          argument,
          reads: new Set(
            [...argument.matchAll(/v\[(\d+)\]/g)].map((m) => m[1]),
          ),
          uses: [],
        };
        cache.set(argument, group);
        groups.push(group);
      }
      group.uses.push({ line, begin, end, trig: call[1] });
      calls.lastIndex = end;
    }
    for (const [argument, group] of cache) {
      if (group.reads.has(assignment[1])) cache.delete(argument);
    }
  }
  const edits = lines.map(() => []);
  const declarations = lines.map(() => []);
  let count = 0;
  for (const group of groups) {
    const sine = group.uses.filter((use) => use.trig === "sinf");
    const cosine = group.uses.filter((use) => use.trig === "cosf");
    // Leave one-off pairs to the later guarded/deferred passes. Only extend
    // reuse that would already introduce a local, preserving their skip paths.
    if (sine.length < 2 && cosine.length < 2) continue;
    const name = `point_trig_${count++}`;
    const paired = sine.length > 0 && cosine.length > 0;
    declarations[group.uses[0].line].push(
      paired
        ? `    float ${name}_s, ${name}_c;\n    milk_fast_sincos(${group.argument}, &${name}_s, &${name}_c);`
        : `    float ${name} = ${group.uses[0].trig}(${group.argument});`,
    );
    for (const use of group.uses) {
      const value = paired ? name + (use.trig === "sinf" ? "_s" : "_c") : name;
      edits[use.line].push({ ...use, name: value });
    }
  }
  return lines
    .map((line, i) => {
      for (const edit of edits[i].sort((a, b) => b.begin - a.begin)) {
        line = line.slice(0, edit.begin) + edit.name + line.slice(edit.end);
      }
      return [...declarations[i], line].join("\n");
    })
    .join("\n");
}
