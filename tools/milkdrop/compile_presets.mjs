import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { fileURLToPath } from "node:url";
import {
  loadLatestBenchmark,
  selectPlaybackPresets,
} from "./benchmark_reports.mjs";
import {
  objectFields,
  frameInputs,
  qVariables,
  tVariables,
  objectPointRegisters,
  objectLimits,
  objectBuiltin,
  OBJECT_VARIABLES,
  OBJECT_INSTANCES,
  compileObjects,
} from "./preset_objects.mjs";
export const VARIABLE_LIMIT = 192;
export const outputs = [
  "zoom",
  "zoomexp",
  "rot",
  "warp",
  "cx",
  "cy",
  "dx",
  "dy",
  "sx",
  "sy",
  "decay",
  "wave_r",
  "wave_g",
  "wave_b",
  "wave_x",
  "wave_y",
  "wave_a",
  "wave_mode",
  "wave_scale",
  "wave_smoothing",
  "wave_mystery",
  "wave_dots",
  "wave_thick",
  "wave_additive",
  "wave_brighten",
  "wave_mod_alpha",
  "wave_mod_start",
  "wave_mod_end",
  "ob_size",
  "ob_r",
  "ob_g",
  "ob_b",
  "ob_a",
  "ib_size",
  "ib_r",
  "ib_g",
  "ib_b",
  "ib_a",
  "warp_speed",
  "warp_scale",
  "wrap",
  "echo_zoom",
  "echo_alpha",
  "echo_orient",
  "shader",
  "darken_center",
  "mv_x",
  "mv_y",
  "mv_dx",
  "mv_dy",
  "mv_l",
  "mv_r",
  "mv_g",
  "mv_b",
  "mv_a",
  "gamma",
  "invert",
  "darken",
  "brighten",
  "solarize",
];
export const inputs = [...frameInputs, "x", "y", "rad", "ang"];
export const builtin = [...outputs, ...inputs, ...qVariables];
const mappings = {
  gamma: "fGammaAdj",
  invert: "bInvert",
  darken: "bDarken",
  brighten: "bBrighten",
  solarize: "bSolarize",
  zoomexp: "fZoomExponent",
  decay: "fDecay",
  wave_a: "fWaveAlpha",
  wave_mode: "nWaveMode",
  wave_scale: "fWaveScale",
  wave_smoothing: "fWaveSmoothing",
  wave_mystery: "fWaveParam",
  wave_dots: "bWaveDots",
  wave_thick: "bWaveThick",
  wave_additive: "bAdditiveWaves",
  wave_brighten: "bMaximizeWaveColor",
  wave_mod_alpha: "bModWaveAlphaByVolume",
  wave_mod_start: "fModWaveAlphaStart",
  wave_mod_end: "fModWaveAlphaEnd",
  warp_speed: "fWarpAnimSpeed",
  warp_scale: "fWarpScale",
  wrap: "bTexWrap",
  echo_zoom: "fVideoEchoZoom",
  echo_alpha: "fVideoEchoAlpha",
  echo_orient: "nVideoEchoOrientation",
  shader: "fShader",
  darken_center: "bDarkenCenter",
  mv_x: "nMotionVectorsX",
  mv_y: "nMotionVectorsY",
};
const defaults = {
  gamma: 1,
  mv_x: 12,
  mv_y: 9,
  mv_l: 0.9,
  mv_r: 1,
  mv_g: 1,
  mv_b: 1,
  echo_zoom: 1,
  zoom: 1,
  zoomexp: 1,
  cx: 0.5,
  cy: 0.5,
  sx: 1,
  sy: 1,
  decay: 0.98,
  wave_r: 1,
  wave_g: 1,
  wave_b: 1,
  wave_x: 0.5,
  wave_y: 0.5,
  wave_a: 1,
  wave_scale: 1,
  wave_mod_start: 0.75,
  wave_mod_end: 1.25,
  warp_speed: 1,
  warp_scale: 1,
};
const functions = {
  sin: [1, "sinf"],
  cos: [1, "cosf"],
  tan: [1, "tanf"],
  abs: [1, "fabsf"],
  sqrt: [1, "milk_sqrt"],
  pow: [2, "milk_pow"],
  min: [2, "fminf"],
  max: [2, "fmaxf"],
  above: [2, null],
  below: [2, null],
  equal: [2, "milk_equal"],
  bnot: [1, null],
  band: [2, null],
  bor: [2, null],
  if: [3, null],
  int: [1, "truncf"],
  sqr: [1, "milk_sqr"],
  sign: [1, "milk_sign"],
  atan: [1, "atanf"],
  atan2: [2, "atan2f"],
  asin: [1, "milk_asin"],
  acos: [1, "milk_acos"],
  exp: [1, "milk_exp"],
  log: [1, "milk_log"],
  log10: [1, "milk_log10"],
  sigmoid: [2, "milk_sigmoid"],
  rand: [1, "milk_rand"],
};
export class Parser {
  constructor(
    code,
    variables = new Map(builtin.map((n, i) => [n, i])),
    policy = {},
  ) {
    this.policy = policy;
    this.readonly = policy.readonly || inputs;
    this.vars = variables;
    this.assigned = new Set();
    this.ops = 0;
    this.p = 0;
    this.temps = 0;
    const text = code.replace(/\/\/[^\n]*/g, "");
    this.tokens = [];
    let i = 0;
    while (i < text.length) {
      const m =
        /^(\s+|(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?|[a-z_]\w*|[+\-*/%^&|=(),;])/i.exec(
          text.slice(i),
        );
      if (!m)
        throw Error(
          `unsupported equation syntax near ${text.slice(i, i + 24)}`,
        );
      i += m[0].length;
      if (!/^\s/.test(m[0])) this.tokens.push(m[0].toLowerCase());
    }
  }
  peek() {
    return this.tokens[this.p];
  }
  take(t) {
    if (this.peek() !== t) throw Error(`expected ${t}, got ${this.peek()}`);
    this.p++;
  }
  variable(n) {
    if (!this.policy.object && n === "wave_usedots") n = "wave_dots";
    if (this.policy.object && n === "num_inst") n = "instances";
    if (n === "progress") throw Error("unsupported input progress");
    if (!this.vars.has(n)) {
      if (/^q\d+$/.test(n)) throw Error(`unsupported q register ${n}`);
      this.vars.set(n, this.vars.size);
    }
    return `v[${this.vars.get(n)}]`;
  }
  sequence(args) {
    if (args.length < 2 || !args.some((x) => x.includes("milk_rand(")))
      return { args, prefix: "" };
    const names = args.map(() => "r" + this.temps++);
    return {
      args: names,
      prefix: names.map((n, i) => "(" + n + "=" + args[i] + "),").join(""),
    };
  }
  expr(min = 0, depth = 0) {
    if (depth > 64) throw Error("expression nesting limit");
    let t = this.tokens[this.p++],
      left;
    this.ops++;
    if (t === undefined) throw Error("incomplete equation");
    if (t === "+" || t === "-")
      left = { c: `(${t}${this.expr(40, depth + 1).c})` };
    else if (t === "(") {
      left = this.expr(0, depth + 1);
      this.take(")");
    } else if (/^(\d|\.)/.test(t)) {
      const n = Number(t);
      if (!Number.isFinite(n) || Math.abs(n) > 3.4e38)
        throw Error("invalid literal");
      left = { c: cfloat(n) };
    } else if (/^[a-z_]/.test(t)) {
      if (this.peek() === "(") {
        this.p++;
        let args = [];
        if (this.peek() !== ")") {
          do {
            args.push(this.expr(0, depth + 1).c);
            if (this.peek() !== ",") break;
            this.p++;
          } while (true);
        }
        this.take(")");
        if (!functions[t] || args.length !== functions[t][0])
          throw Error(`unsupported function/arity ${t}/${args.length}`);
        const ordered = t === "if" ? { args, prefix: "" } : this.sequence(args);
        args = ordered.args;
        const [a, b, c] = args;
        let value;
        if (t === "if") value = `(milk_truth(${a})?(${b}):(${c}))`;
        else if (t === "above" || t === "below")
          value = `((${a})${t === "above" ? ">" : "<"}(${b})?1.0f:0.0f)`;
        else if (t === "bnot") value = `(!milk_truth(${a})?1.0f:0.0f)`;
        else if (t === "band" || t === "bor")
          value = `(milk_truth(${a})${t === "band" ? "&" : "|"}milk_truth(${b})?1.0f:0.0f)`;
        else
          value = `${functions[t][1]}(${t === "rand" ? "rng," : ""}${args.join(",")})`;
        left = { c: ordered.prefix ? `(${ordered.prefix}${value})` : value };
      } else left = { c: this.variable(t), name: t };
    } else throw Error(`unexpected token ${t}`);
    const precedence = {
      "=": 1,
      "|": 3,
      "&": 4,
      "+": 10,
      "-": 10,
      "*": 20,
      "/": 20,
      "%": 20,
      "^": 30,
    };
    while (precedence[this.peek()] >= min) {
      const op = this.tokens[this.p++],
        level = precedence[op];
      const right = this.expr(
        level + (op === "=" || op === "^" ? 0 : 1),
        depth + 1,
      );
      this.ops++;
      if (op === "=") {
        if (depth > 0) throw Error("nested assignment is unsupported");
        if (!left.name) throw Error("assignment requires variable");
        if (this.readonly.includes(left.name))
          throw Error(`read-only input ${left.name}`);
        if (
          !this.policy.object &&
          /^(mv_|echo_|blur|gamma|shader|brighten|darken|invert|solarize)/.test(
            left.name,
          ) &&
          !outputs.includes(left.name) &&
          left.name !== "echo_orientation"
        )
          throw Error(`unsupported effect ${left.name}`);

        this.assigned.add(left.name);
        left = { c: `(${left.c}=milk_finite(${right.c}))` };
      } else {
        const ordered = this.sequence([left.c, right.c]);
        left.c = ordered.args[0];
        right.c = ordered.args[1];
        left = {
          c:
            op === "&" || op === "|"
              ? `milk_bit${op === "&" ? "and" : "or"}(${left.c},${right.c})`
              : op === "/"
                ? `milk_div(${left.c},${right.c})`
                : op === "%"
                  ? `milk_mod(${left.c},${right.c})`
                  : op === "^"
                    ? `milk_pow(${left.c},${right.c})`
                    : `(${left.c}${op}${right.c})`,
        };
        if (ordered.prefix) left.c = `(${ordered.prefix}${left.c})`;
      }
    }
    return left;
  }
  parse() {
    let out = "";
    while (this.p < this.tokens.length) {
      if (this.peek() === ";") {
        this.p++;
        continue;
      }
      this.randomCalls = 0;
      out += `    (void)${this.expr().c};\n`;
      if (this.peek() !== undefined) this.take(";");
    }
    return (
      (this.temps
        ? `    float ${Array.from({ length: this.temps }, (_, i) => "r" + i).join(", ")};\n`
        : "") + out
    );
  }
}
function cfloat(n) {
  return `${Number(n).toExponential(9)}f`;
}
export function parsePreset(text) {
  const f = new Map();
  for (const l of text.split(/\r?\n/)) {
    const m = l.match(/^\s*([^;=]+?)\s*=(.*)$/);
    // Original preset loading resolves repeated field names to the first value.
    if (m && !f.has(m[1])) {
      f.set(m[1], m[2]);
    }
  }
  return f;
}
function numeric(f, k, d = 0) {
  if (!f.has(k)) return d;
  // Like upstream GetFastFloat, read a numeric prefix and use the field default
  // when no number can be read (for example rot=-). Keep non-finite values out.
  const text = f.get(k).trim();
  const n = Number.parseFloat(text);
  if (Number.isNaN(n) && !/^[+-]?(?:nan|inf)/i.test(text)) return d;
  if (!Number.isFinite(n) || Math.abs(n) > 3.4e38)
    throw Error(`invalid numeric field ${k}`);
  return n;
}
function code(f, prefix) {
  const fragments = [];
  // Upstream reads consecutive keys from 1 and stops at the first missing key.
  for (let line = 1; f.has(prefix + line); ++line) {
    // Strip comments per fragment, then remove linefeed markers entirely.
    fragments.push(f.get(prefix + line).replace(/(?:\/\/|\\\\).*$/, ""));
  }
  return fragments.join("");
}
export function compile(
  text,
  limits = {
    variables: VARIABLE_LIMIT,
    frameOperations: 4096,
    vertexOperations: 1024,
  },
) {
  limits = {
    variables: VARIABLE_LIMIT,
    frameOperations: 4096,
    vertexOperations: 1024,
    ...limits,
  };
  for (const [key, value] of Object.entries(limits))
    if (!Number.isSafeInteger(value) || value < 1)
      throw Error(`invalid budget ${key}`);
  const f = parsePreset(text);
  for (const k of f.keys())
    if (/^(warp|comp)_\d+$/.test(k)) throw Error("warp/composite shader");

  for (const k of ["bRedBlueStereo"])
    if (numeric(f, k) !== 0) throw Error(`unsupported effect ${k}`);
  if (numeric(f, "fGammaAdj", 1) < 0 || numeric(f, "fGammaAdj", 1) > 8)
    throw Error("gamma outside PS2 range [0, 8]");
  if (![0, 1, 2, 3, 4, 5, 6, 7, 8].includes(numeric(f, "nWaveMode")))
    throw Error("unsupported waveform mode");
  for (const key of ["fWaveSmoothing", "fVideoEchoAlpha", "fShader"]) {
    const value = numeric(f, key);
    if (value < 0 || value > 1) throw Error(`invalid range ${key}`);
  }
  const echoZoom = numeric(f, "fVideoEchoZoom", 1);
  const echoZoomOutsideRange = echoZoom < 0.2 || echoZoom > 100;
  if (![0, 1, 2, 3].includes(numeric(f, "nVideoEchoOrientation")))
    throw Error("invalid echo orientation");
  for (const k of [
    "bMotionVectorsOn",
    "bBrighten",
    "bSolarize",
    "bDarken",
    "bInvert",
    "bDarkenCenter",
    "bTexWrap",
    "bWaveDots",
    "bWaveThick",
    "bAdditiveWaves",
    "bMaximizeWaveColor",
    "bModWaveAlphaByVolume",
  ])
    if (![0, 1].includes(numeric(f, k))) throw Error(`invalid flag ${k}`);
  // Blur metadata is inert in the fixed-function path; shader files fail above.
  const inert = new Set([
    "b1n",
    "b2n",
    "b3n",
    "b1x",
    "b2x",
    "b3x",
    "b1ed",
    "bMotionVectorsOn",
    "fRating",
    "fGammaAdj",
    "fVideoEchoZoom",
    "fVideoEchoAlpha",
    "nVideoEchoOrientation",
    "fShader",
    "bBrighten",
    "bDarken",
    "bSolarize",
    "bInvert",
    "bDarkenCenter",
    "bRedBlueStereo",
    "nMotionVectorsX",
    "nMotionVectorsY",
    "mv_x",
    "mv_y",
    "mv_dx",
    "mv_dy",
    "mv_l",
    "mv_r",
    "mv_g",
    "mv_b",
    "mv_a",
  ]);
  const known = new Set(outputs.map((k) => mappings[k] || k));
  for (const k of f.keys())
    if (
      !known.has(k) &&
      !inert.has(k) &&
      !/^per_(frame_init|frame|pixel)_\d+$/.test(k) &&
      !/^((wave|shape)code_\d+_|(wave|shape)_\d+_)/.test(k)
    )
      throw Error(`unsupported field ${k}`);
  const objects = compileObjects(f, limits, Parser, numeric, code);
  const vars = new Map(builtin.map((n, i) => [n, i]));
  const compiled = [];
  let writesEchoAlpha = false;
  for (const prefix of ["per_frame_init_", "per_frame_", "per_pixel_"]) {
    const p = new Parser(code(f, prefix), vars, { readonly: [] }),
      c = p.parse();
    writesEchoAlpha ||= p.assigned.has("echo_alpha");
    if (
      p.ops >
      (prefix === "per_pixel_"
        ? limits.vertexOperations
        : limits.frameOperations)
    )
      throw Error(`${prefix} operation budget`);
    if (vars.size > Math.min(VARIABLE_LIMIT, limits.variables))
      throw Error("variable budget");
    compiled.push({ c, operations: p.ops });
  }
  if (
    echoZoomOutsideRange &&
    (numeric(f, "fVideoEchoAlpha") !== 0 || writesEchoAlpha)
  )
    throw Error("echo zoom outside PS2 range [0.2, 100]");
  return {
    defaults: outputs.map((k) =>
      numeric(
        f,
        mappings[k] || k,
        k === "mv_a" ? numeric(f, "bMotionVectorsOn") : defaults[k] || 0,
      ),
    ),
    compiled,
    objects,
    customSpectrum: objects.some(
      (object) =>
        object.type === "wave" &&
        object.defaults[objectFields.indexOf("spectrum")] !== 0,
    ),
    variables: [...vars.keys()],
  };
}
function walk(root) {
  return fs
    .readdirSync(root, { withFileTypes: true })
    .filter((e) => e.name !== ".git")
    .flatMap((e) =>
      e.isDirectory()
        ? walk(path.join(root, e.name))
        : /\.milk2?$/i.test(e.name)
          ? [path.join(root, e.name)]
          : [],
    );
}
function writeChanged(filename, text) {
  if (!fs.existsSync(filename) || fs.readFileSync(filename, "utf8") !== text)
    fs.writeFileSync(filename, text);
}
export function generate(
  root,
  benchmarkDirectory,
  out,
  selection = "playback",
) {
  if (!["playback", "all-compatible", "quick"].includes(selection))
    throw Error("unknown preset selection: " + selection);
  const { filename, report: benchmark } =
    loadLatestBenchmark(benchmarkDirectory);
  const measured = benchmark.results;
  const allowed = new Set(
    selection === "quick"
      ? measured
          .filter((result) => result.quick === true)
          .map((result) => result.file)
      : selectPlaybackPresets(benchmark),
  );
  if (selection === "quick" && allowed.size !== 9)
    throw Error(
      "Quick selection requires a full benchmark with at least nine presets",
    );
  const entries = [],
    included = [];
  const files = fs.existsSync(root) ? walk(root).sort() : [];
  for (const filename of files) {
    const name = path.relative(root, filename).split(path.sep).join("/");
    let result, reason;
    try {
      if (!/\.milk$/i.test(filename)) throw Error("double preset");
      result = compile(fs.readFileSync(filename, "latin1"));
    } catch (e) {
      reason = e.message;
    }
    if (!reason && selection !== "all-compatible" && !allowed.has(name))
      reason = "not selected by benchmark";
    const hash = crypto
      .createHash("sha256")
      .update(fs.readFileSync(filename))
      .digest("hex");
    const measurement = measured.find(
      (result) => result.file === name && result.sha256 === hash,
    );
    if (!reason && selection !== "all-compatible" && !measurement)
      reason = "preset changed since benchmark";
    entries.push({
      file: name,
      status: reason ? "SKIPPED" : "INCLUDED",
      reason,
      sha256: hash,
    });
    if (!reason) {
      const benchmarkFps =
        Number.isFinite(measurement?.steady_fps) && measurement.steady_fps > 0
          ? measurement.steady_fps
          : 0;
      entries[entries.length - 1].benchmark_fps = benchmarkFps;
      included.push({ name, ...result, benchmarkFps });
    }
  }
  for (const name of selection === "all-compatible" ? [] : allowed)
    if (!entries.some((e) => e.file === name))
      entries.push({
        file: name,
        status: "SKIPPED",
        reason: "measured file missing",
      });
  fs.mkdirSync(out, { recursive: true });
  const report = {
    preset_root: path.resolve(root),
    benchmark: filename,
    benchmark_dir: path.resolve(benchmarkDirectory),
    selection,
    total: entries.length,
    included: included.length,
    entries,
  };
  writeChanged(
    path.join(out, "preset-report.json"),
    JSON.stringify(report, null, 2) + "\n",
  );
  const rejected = entries.filter(
    (e) =>
      selection !== "all-compatible" &&
      allowed.has(e.file) &&
      e.status !== "INCLUDED",
  );
  if (rejected.length)
    throw Error(
      "Benchmark selection rejected:\n" +
        rejected.map((e) => `${e.file}: ${e.reason}`).join("\n"),
    );
  const header = `/* Generated; edit benchmark/source presets, not this file. */
#pragma once

#define MILK_VARIABLES ${VARIABLE_LIMIT}
#define MILK_FRAME_INPUTS ${frameInputs.length}
#define MILK_Q_VARIABLES ${qVariables.length}
#define MILK_T_VARIABLES ${tVariables.length}
#define MILK_OBJECT_POINT_REGISTERS ${objectPointRegisters.length}
#define MILK_OBJECTS ${Object.values(objectLimits).reduce((sum, limit) => sum + limit, 0)}
#define MILK_OBJECT_VARIABLES ${OBJECT_VARIABLES}
#define MILK_OBJECT_INSTANCES ${OBJECT_INSTANCES}
#define MILK_OBJECT_FIELDS ${objectFields.length}
#define MILK_OBJECT_BUILTINS ${objectBuiltin.length}
enum
{
${objectBuiltin.map((name, index) => "    MO_" + name.toUpperCase() + " = " + index + ",").join("\n")}
};
#define MILK_PRESET_COUNT ${included.length}
#define MILK_OUTPUT_COUNT ${outputs.length}
#define MILK_BUILTIN_COUNT ${builtin.length}

enum
{
${builtin.map((name, index) => `    ML_${name.toUpperCase()} = ${index},`).join("\n")}
};

`;
  let source =
    '#include "milkdrop/milk.h"\n#include "milkdrop/milk_wave_points.h"\n#include "milkdrop/frame_rate.h"\n#include <math.h>\n#include <stddef.h>\n#define sinf milk_fast_sin\n#define cosf milk_fast_cos\n\n';
  included.forEach((preset, index) =>
    preset.compiled.forEach((code, stage) => {
      source += `static void program_${index}_${stage}(float* v, uint32_t* rng)
{
    (void)v;
    (void)rng;
${code.c}}

`;
    }),
  );
  included.forEach((preset, index) => {
    if (!preset.objects.length) return;
    preset.objects.forEach((object, oi) =>
      object.compiled.forEach((code, stage) => {
        if (object.type === "wave" && stage === 2) {
          source += `MILK_WAVE_POINT_LOOP(object_${index}_${oi}_${stage},
${code.c})

`;
          return;
        }
        source += `static void object_${index}_${oi}_${stage}(float* v, uint32_t* rng)\n{\n    (void)v;\n    (void)rng;\n${code.c}}\n\n`;
      }),
    );
    source += `static const MilkObjectProgram objects_${index}[] = {\n`;
    preset.objects.forEach((object, oi) => {
      source += `    { ${object.type === "shape" ? 0 : 1}, ${object.index}, { ${object.defaults.map(cfloat).join(", ")} }, object_${index}_${oi}_0, object_${index}_${oi}_1, ${object.type === "wave" ? "object_" + index + "_" + oi + "_2" : "NULL"} },\n`;
    });
    source += "};\n";
  });
  source += `const MilkProgram milk_programs[${Math.max(1, included.length)}] = {
`;
  if (!included.length) source += "    { 0 }\n";
  included.forEach((preset, index) => {
    const defaults = preset.defaults
      .map(
        (value, slot) => `            ${cfloat(value)}, /* ${outputs[slot]} */`,
      )
      .join("\n");
    source += `    {
        ${JSON.stringify(path.basename(preset.name, ".milk"))},
        {
${defaults}
        },
        program_${index}_0,
        program_${index}_1,
        program_${index}_2,
        ${preset.objects.length}, ${preset.objects.length ? "objects_" + index : "NULL"},
        ${preset.customSpectrum ? 1 : 0},
        MILK_FRAME_RATE_MAXIMUM(${cfloat(preset.benchmarkFps)}),
    },
`;
  });
  source += "};\n";
  writeChanged(path.join(out, "milk_presets.h"), header);
  writeChanged(path.join(out, "milk_presets.c"), source);
  console.log(
    `Preset compiler: ${included.length} INCLUDED, ${entries.length - included.length} SKIPPED; ${path.join(out, "preset-report.json")}`,
  );
}
if (
  process.argv[1] &&
  path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  try {
    if (process.argv.length < 5 || process.argv.length > 6)
      throw Error(
        "usage: compile_presets.mjs root benchmark-directory output [playback|all-compatible|quick]",
      );
    generate(...process.argv.slice(2));
  } catch (e) {
    console.error(e.message);
    process.exitCode = 1;
  }
}
