/* Inventory the user's local pack; this does not claim renderer compatibility. */
import fs from "node:fs";
import path from "node:path";
const root = path.resolve(
  process.argv[2] ||
    new URL("../../third_party/presets-milkdrop-original", import.meta.url)
      .pathname,
);
function walk(dir) {
  return fs
    .readdirSync(dir, { withFileTypes: true })
    .filter((e) => e.name !== ".git")
    .flatMap((e) =>
      e.isDirectory()
        ? walk(path.join(dir, e.name))
        : /\.milk2?$/i.test(e.name)
          ? [path.join(dir, e.name)]
          : [],
    );
}
const functions = {},
  entries = [];
for (const filename of walk(root).sort()) {
  const text = fs.readFileSync(filename, "latin1");
  const fields = new Map();
  for (const line of text.split(/\r?\n/)) {
    const match = line.match(/^\s*([^;=]+?)\s*=\s*(.*)$/);
    if (match) fields.set(match[1], match[2]);
  }
  const shaders = [...fields.keys()].filter((k) => /^(warp|comp)_\d+$/.test(k));
  const code = [...fields]
    .filter(([k]) => /^(per_frame|per_pixel|wave_\d+_|shape_\d+_)/.test(k))
    .map(([, v]) => v.replace(/\/\/.*$/, ""))
    .join("\n");
  const calls = [
    ...new Set(
      [...code.matchAll(/\b([a-zA-Z_]\w*)\s*\(/g)].map((m) =>
        m[1].toLowerCase(),
      ),
    ),
  ].sort();
  for (const f of calls) functions[f] = (functions[f] || 0) + 1;
  const enabled = (prefix) =>
    [...fields].filter(
      ([k, v]) =>
        new RegExp("^" + prefix + "code_\\d+_enabled$").test(k) &&
        Number(v) !== 0,
    ).length;
  entries.push({
    file: path.relative(root, filename),
    shader: shaders.length > 0,
    double: /\.milk2$/i.test(filename),
    customWaves: enabled("wave"),
    customShapes: enabled("shape"),
    equations: code.trim().length > 0,
    waveMode: Number(fields.get("nWaveMode") || 0),
    functions: calls,
    textureReferences: [
      ...new Set(
        [...text.matchAll(/\bsampler_([a-zA-Z_]\w*)/g)].map((m) => m[1]),
      ),
    ].sort(),
  });
}
const report = {
  root,
  total: entries.length,
  shader: entries.filter((e) => e.shader).length,
  withoutShader: entries.filter((e) => !e.shader && !e.double).length,
  double: entries.filter((e) => e.double).length,
  withCustomWaves: entries.filter((e) => e.customWaves).length,
  withCustomShapes: entries.filter((e) => e.customShapes).length,
  functions: Object.fromEntries(
    Object.entries(functions).sort((a, b) => b[1] - a[1]),
  ),
  entries,
};
if (process.argv[3])
  fs.writeFileSync(process.argv[3], JSON.stringify(report, null, 2) + "\n");
console.log(JSON.stringify({ ...report, entries: undefined }, null, 2));
