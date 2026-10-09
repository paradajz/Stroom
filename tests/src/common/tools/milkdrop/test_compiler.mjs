import { unityRunner, writeFixture } from "../../support/unity.mjs";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import {
  Parser,
  compile,
  generate,
  builtin,
  outputs,
  inputs,
  VARIABLE_LIMIT,
} from "../../../../../tools/milkdrop/compile_presets.mjs";
import {
  frameInputs,
  qVariables,
  tVariables,
  objectPointRegisters,
  objectBuiltin,
  objectLimits,
} from "../../../../../tools/milkdrop/preset_objects.mjs";
const [output] = process.argv.slice(2);
let source;

function benchmarkFps(directory) {
  return JSON.parse(
    fs.readFileSync(path.join(directory, "preset-report.json"), "utf8"),
  ).entries.find((entry) => entry.status === "INCLUDED").benchmark_fps;
}

function rejects(text, reason) {
  assert.throws(() => compile(text), reason);
}
const data =
  "[preset00]\nfGammaAdj=1\nnWaveMode=0\nfDecay=.97\nper_frame_1=a=2+3*4;\nper_frame_2=wave_r=if(above(a,10),sin(time),.5);\nper_pixel_1=zoom=1+.01*sin(rad);";
const p = compile(data);
assert.equal(
  compile(data + "\nbSolarize=1").defaults[outputs.indexOf("solarize")],
  1,
);
compile(data + "\nper_frame_3=solarize=above(bass,1);");
rejects(data + "\nbSolarize=2", /invalid flag/);
// Missing numbers terminate each equation block; existing empty fragments do not.
for (const [prefix, setup] of [
  ["per_frame_init_", ""],
  ["per_frame_", ""],
  ["per_pixel_", ""],
  ["wave_0_init", "wavecode_0_enabled=1\n"],
  ["wave_0_per_frame", "wavecode_0_enabled=1\n"],
  ["wave_0_per_point", "wavecode_0_enabled=1\n"],
  ["shape_0_init", "shapecode_0_enabled=1\n"],
  ["shape_0_per_frame", "shapecode_0_enabled=1\n"],
]) {
  const first = setup + prefix + "1=q1=1;";
  assert.deepEqual(
    compile(first + "\n" + prefix + "3=invalid(!"),
    compile(first),
  );
  assert.deepEqual(compile(setup + prefix + "2=invalid(!"), compile(setup));
  assert.deepEqual(
    compile(first + "\n" + prefix + "2=\n" + prefix + "3=q1=2;"),
    compile(first + "\n" + prefix + "2=q1=2;"),
  );
  assert.deepEqual(
    compile(setup + prefix + "2=q1=2;\n" + prefix + "1=q1=1;"),
    compile(first + "\n" + prefix + "2=q1=2;"),
  );
}

// Numbered equation fragments can split identifiers, as in the original loader.
const split = compile("per_frame_1=wave_r=tre\nper_frame_2=b_att;");
// Whitespace at fragment boundaries must not merge separate tokens.
rejects("per_frame_1=wave_r=tre \nper_frame_2=b_att;", /expected ;/);
const joined = compile("per_frame_1=wave_r=treb_att;");
assert.deepEqual(split.compiled, joined.compiled);
assert.deepEqual(
  compile("per_frame_1=wave_r=1;// ignored\nper_frame_2=wave_g=2;").compiled,
  compile("per_frame_1=wave_r=1;wave_g=2;").compiled,
);
compile(data + "\nfVideoEchoZoom=0\nfVideoEchoAlpha=0");
for (const stage of ["per_frame_init_", "per_pixel_"]) {
  rejects(
    `fVideoEchoZoom=0\nfVideoEchoAlpha=0\n${stage}1=echo_alpha=1;`,
    /echo zoom/,
  );
}
rejects(data + "\nfVideoEchoZoom=0\nfVideoEchoAlpha=.5", /echo zoom/);
rejects(
  data + "\nfVideoEchoZoom=0\nfVideoEchoAlpha=0\nper_frame_3=echo_alpha=1;",
  /echo zoom/,
);
assert.equal(
  compile(data + "\nbDarken=1").defaults[outputs.indexOf("darken")],
  1,
);
compile(data + "\nper_frame_3=darken=above(bass,1);");
assert.equal(
  compile(data + "\nbBrighten=1").defaults[outputs.indexOf("brighten")],
  1,
);
compile(data + "\nper_frame_3=brighten=above(bass,1);");
rejects(data + "\nbBrighten=2", /invalid flag/);
rejects(data + "\nbDarken=2", /invalid flag/);
assert.deepEqual(inputs.slice(0, frameInputs.length), frameInputs);
const objectInputOffset = objectBuiltin.indexOf(frameInputs[0]);
assert.deepEqual(
  objectBuiltin.slice(
    objectInputOffset,
    objectInputOffset + frameInputs.length,
  ),
  frameInputs,
);
const allObjects = Object.entries(objectLimits).flatMap(([type, limit]) =>
  Array.from({ length: limit }, (_, index) => `${type}code_${index}_enabled=1`),
);
assert.equal(
  compile(data + "\n" + allObjects.join("\n")).objects.length,
  allObjects.length,
);
for (const [type, limit] of Object.entries(objectLimits)) {
  compile(data + `\n${type}code_${limit}_enabled=0`);
  rejects(
    data + `\n${type}code_${limit}_enabled=1`,
    /custom object count budget/,
  );
}
assert.equal(p.variables.length, builtin.length + 1);
assert(p.compiled[1].c.includes("?"));
assert.deepEqual(compile(data), p);
assert.equal(p.customSpectrum, false);
for (const enabled of [0, 1]) {
  for (const spectrum of [0, 1]) {
    assert.equal(
      compile(
        data +
          `\nwavecode_0_enabled=${enabled}\nwavecode_0_bSpectrum=${spectrum}`,
      ).customSpectrum,
      Boolean(enabled && spectrum),
    );
  }
}
rejects(data + "\nwarp_1=shader_body{}", /shader/);
compile(
  data +
    "\nfVideoEchoAlpha=.5\nfVideoEchoZoom=2\nnVideoEchoOrientation=3\nfWaveSmoothing=.6\nper_frame_3=echo_alpha=.25;echo_zoom=1.2;echo_orient=2;",
);
for (let mode = 0; mode <= 8; ++mode)
  compile(data.replace("nWaveMode=0", "nWaveMode=" + mode) + "\nfShader=.75");
for (const mode of [-1, 9, 1.5])
  rejects(data.replace("nWaveMode=0", "nWaveMode=" + mode), /waveform/);
compile(
  data +
    "\nbDarkenCenter=1\nmv_a=.5\nper_frame_3=mv_x=12+bass;mv_y=9;mv_dx=.1;mv_dy=.2;mv_l=2;mv_r=.1;mv_g=.2;mv_b=.3;mv_a=.4;darken_center=above(bass,1);",
);
assert.equal(
  compile(data + "\nbMotionVectorsOn=1").defaults[outputs.indexOf("mv_a")],
  1,
);
assert.equal(
  compile(data + "\nbMotionVectorsOn=1\nmv_a=0").defaults[
    outputs.indexOf("mv_a")
  ],
  0,
);
assert.equal(
  compile(data + "\nnMotionVectorsX=6.4\nnMotionVectorsY=9.6").defaults[
    outputs.indexOf("mv_x")
  ],
  6.4,
);
rejects(data + "\nbMotionVectorsOn=2", /flag/);
rejects(data + "\nbDarkenCenter=2", /flag/);
rejects(data + "\nper_frame_3=mv_unknown=1;", /effect/);
const manyVariables =
  "\nper_frame_3=" +
  Array.from({ length: VARIABLE_LIMIT }, (_, i) => "custom" + i + "=1;").join(
    "",
  );
rejects(data + manyVariables, /variable budget/);
rejects(data + "\nfShader=1.1", /range/);
rejects(data + "\nfShader=-.1", /range/);
compile(data + "\nper_frame_3=shader=.5;");
rejects(data + "\nfVideoEchoAlpha=1.5", /range/);
rejects(data + "\nfVideoEchoZoom=0\nfVideoEchoAlpha=.5", /zoom/);
rejects(data + "\nnVideoEchoOrientation=4", /orientation/);
rejects(data + "\nfWaveSmoothing=-.1", /range/);
rejects(data + "\nfWaveSmoothing=1.1", /range/);
rejects(data + "\nper_frame_3=echo_unknown=1;", /effect/);
rejects(data + "\nper_pixel_2=foo=unsupported(1);", /function/);
compile(data + "\nper_pixel_2=foo=1 & 2;");
compile(data + "\nper_frame_3=time=time*.1;");
compile(data + "\nper_frame_3=gamma=2;");
assert.equal(compile(data + "\nwavecode_0_enabled=1").objects.length, 1);
assert.equal(
  compile(data + "\nfGammaAdj=2").defaults[outputs.indexOf("gamma")],
  1,
);
assert.equal(
  compile("per_frame_1=wave_r=.2;\nper_frame_1=unsupported(1);").compiled[1].c,
  compile("per_frame_1=wave_r=.2;").compiled[1].c,
);
for (const text of ["-", "", "not-a-number"]) {
  assert.equal(compile("rot=" + text).defaults[outputs.indexOf("rot")], 0);
  assert.equal(
    compile("fGammaAdj=" + text).defaults[outputs.indexOf("gamma")],
    1,
  );
}
assert.equal(
  compile("fGammaAdj=1.5 trailing text").defaults[outputs.indexOf("gamma")],
  1.5,
);
for (const text of [
  "NaN",
  "nan(payload)",
  "Infinity",
  "-inf",
  "1e400",
  "3.5e38",
])
  rejects("rot=" + text, /numeric/);
for (const name of [
  "Illusion & Rovastar - Clouded Bottle",
  "Idiot24-7 - Ascending to heaven 2",
])
  compile(
    fs.readFileSync(
      new URL(
        "../../../../../third_party/presets-milkdrop-original/Milkdrop-Original/" +
          name +
          ".milk",
        import.meta.url,
      ),
      "latin1",
    ),
  );
rejects("fDecay=nan", /numeric/);
rejects(data + "\nunimplemented_effect=1", /unsupported field/);
rejects(data + "\nper_frame_3=a=(b=1)+(b=2);", /nested assignment/);
compile(data + "\nper_frame_3=a=rand(5)+rand(5);");
rejects(data + "\nper_frame_3=a=progress;", /unsupported input/);
compile(data + "\nper_frame_3=wave_smoothing=.5;");
assert.throws(
  () =>
    compile(data, { variables: 128, frameOperations: 1, vertexOperations: 1 }),
  /budget/,
);
new Parser("a=-(2^3); // comment\nb=1e-3;").parse();
const custom = compile(
  data +
    "\nshapecode_0_enabled=1\nshapecode_0_num_inst=2\nshape_0_init1=t1=.2;\nshape_0_per_frame1=x=.5+.1*instance;r=t1;q1=instance;\nwavecode_0_enabled=1\nwave_0_per_point1=x=sample;y=.5+value1;r=q1;",
);
assert.equal(custom.objects.length, 2);
assert.equal(custom.objects[0].compiled.length, 2);
assert.equal(custom.objects[1].compiled.length, 3);
assert(custom.objects[0].compiled[0].c.includes("v["));
rejects(
  data + "\nshapecode_0_enabled=1\nshapecode_0_num_inst=17",
  /instance budget/,
);
rejects(data + "\nwavecode_4_enabled=1", /object count/);
rejects(
  data + "\nwavecode_0_enabled=1\nwavecode_0_unimplemented=1",
  /custom wave field/,
);
rejects(data.replace("fGammaAdj=1", "fGammaAdj=9"), /gamma/);
rejects(data + "\nbInvert=2", /flag/);
for (const flag of ["bRedBlueStereo"])
  rejects(data + "\n" + flag + "=1", /unsupported effect/);
const alias = compile(data + "\nper_frame_3=wave_usedots=1;");
assert(!alias.variables.includes("wave_usedots"));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "stroom-compiler-"));
try {
  const equation = new Parser(
    "a=2+3*4;b=(2+3)*4;c=2^3^2;d=if(above(a,10),a,b);e=-(2^3);f=rand(3)-rand(5);g=if(0,rand(99)+rand(98),7);h=max(rand(4),rand(6));",
  );
  const body = equation.parse();
  const checks = [
    ["a", 14],
    ["b", 20],
    ["c", 512],
    ["d", 14],
    ["e", -8],
    ["f", -102],
    ["g", 7],
    ["h", 406],
  ]
    .map(
      ([name, value]) =>
        `TEST_ASSERT_TRUE_MESSAGE(v[${equation.vars.get(name)}]==${value}, "Equation ${name}");`,
    )
    .join("");
  source = `#include "unity.h"
#include <math.h>

float milk_finite(float x)
{
    return isfinite(x) ? x : 0;
}

int milk_truth(float x)
{
    return fabsf(x) > .00001f;
}

float milk_pow(float a, float b)
{
    return powf(a, b);
}

float milk_rand(unsigned* rng,float range) { return ++*rng*100+range; }
static void generated_equations(void)
{
    unsigned state=0;unsigned* rng=&state;
    float v[192] = { 0 };
${body}
${checks}
    TEST_ASSERT_EQUAL_UINT(4,state);
}
${unityRunner("generated_equations")}
`;
  const root = path.join(dir, "source");
  fs.mkdirSync(root);
  fs.writeFileSync(path.join(root, "test.milk"), data);
  fs.writeFileSync(path.join(root, "bad.milk"), "warp_1=shader_body{}");
  const benchmarkDirectory = path.join(dir, "benchmarks");
  const output = path.join(dir, "generated");
  assert.throws(
    () => generate(root, benchmarkDirectory, output),
    /No benchmark report/,
  );
  fs.mkdirSync(benchmarkDirectory);
  const measurement = (file, classification = "okay") => ({
    file,
    classification,
    quick: null,
    work_avg_us: 1000,
    steady_fps: 60.12,
    sha256: fs.existsSync(path.join(root, file))
      ? createHash("sha256")
          .update(fs.readFileSync(path.join(root, file)))
          .digest("hex")
      : "0".repeat(64),
  });
  const writeReport = (name, results) =>
    fs.writeFileSync(
      path.join(benchmarkDirectory, name),
      JSON.stringify({
        status: "complete",
        current: null,
        configuration: {
          total: results.length,
          render_profile: 0,
          detail_profile: 0,
          wave_profile: 0,
          video_mode: "480p",
          refresh_hz: 59.94006,
        },
        results,
      }),
    );
  writeReport("2026-01-01.json", [measurement("test.milk")]);
  generate(root, benchmarkDirectory, output);
  assert.equal(benchmarkFps(output), 60.12);
  const newer = measurement("test.milk");
  newer.steady_fps = 30.5;
  writeReport("2026-01-02.json", [newer]);
  generate(root, benchmarkDirectory, output);
  assert.equal(benchmarkFps(output), 30.5);
  const first = fs.readFileSync(path.join(output, "milk_presets.c"));
  const stamp = fs.statSync(path.join(output, "milk_presets.c")).mtimeMs;
  generate(root, benchmarkDirectory, output);
  assert.equal(fs.statSync(path.join(output, "milk_presets.c")).mtimeMs, stamp);
  fs.writeFileSync(
    path.join(root, "test.milk"),
    data.replace("fDecay=.97", "fDecay=.95"),
  );
  assert.throws(
    () => generate(root, benchmarkDirectory, output),
    /changed since benchmark/,
  );
  generate(root, benchmarkDirectory, output, "all-compatible");
  assert.equal(benchmarkFps(output), 0);
  assert(!first.equals(fs.readFileSync(path.join(output, "milk_presets.c"))));
  let report = JSON.parse(
    fs.readFileSync(path.join(output, "preset-report.json")),
  );
  assert.equal(report.included, 1);
  assert.equal(report.entries.length, 2);
  for (const entry of report.entries) {
    assert.equal(
      entry.sha256,
      createHash("sha256")
        .update(fs.readFileSync(path.join(root, entry.file)))
        .digest("hex"),
    );
  }
  writeReport("2026-01-02.json", [measurement("bad.milk")]);
  assert.throws(
    () => generate(root, benchmarkDirectory, output),
    /Benchmark selection rejected/,
  );
  writeReport("2026-01-02.json", [measurement("missing.milk")]);
  assert.throws(() => generate(root, benchmarkDirectory, output), /missing/);
  writeReport("2026-01-02.json", [measurement("test.milk", "slow")]);
  generate(root, benchmarkDirectory, output);
  const header = fs.readFileSync(path.join(output, "milk_presets.h"), "utf8");
  assert(header.includes(`#define MILK_FRAME_INPUTS ${frameInputs.length}\n`));
  assert(header.includes(`#define MILK_Q_VARIABLES ${qVariables.length}\n`));
  assert(header.includes(`#define MILK_T_VARIABLES ${tVariables.length}\n`));
  assert(
    header.includes(
      `#define MILK_OBJECT_POINT_REGISTERS ${objectPointRegisters.length}\n`,
    ),
  );
  assert.equal(qVariables.length, 32);
  assert.equal(tVariables.length, 8);
  assert.deepEqual(builtin.slice(builtin.indexOf("q1")), qVariables);
  assert.deepEqual(
    objectBuiltin.slice(
      objectBuiltin.indexOf("q1"),
      objectBuiltin.indexOf("sample"),
    ),
    objectPointRegisters,
  );
  assert.deepEqual(objectPointRegisters, [...qVariables, ...tVariables]);
  assert(header.includes(`#define MILK_OBJECTS ${allObjects.length}\n`));
  assert(
    fs
      .readFileSync(path.join(output, "milk_presets.h"), "utf8")
      .includes("MILK_PRESET_COUNT 0"),
  );
  generate(root, benchmarkDirectory, output, "all-compatible");
  report = JSON.parse(fs.readFileSync(path.join(output, "preset-report.json")));
  assert.equal(report.included, 1);
  assert.equal(
    report.entries.find((e) => e.file === "test.milk").status,
    "INCLUDED",
  );
  assert.equal(
    report.entries.find((e) => e.file === "bad.milk").status,
    "SKIPPED",
  );
  fs.renameSync(path.join(root, "test.milk"), path.join(root, "test.MILK"));
  fs.writeFileSync(path.join(root, "double.MILK2"), data);
  generate(root, benchmarkDirectory, output, "all-compatible");
  report = JSON.parse(fs.readFileSync(path.join(output, "preset-report.json")));
  assert.equal(
    report.entries.find((e) => e.file === "test.MILK").status,
    "INCLUDED",
  );
  assert.equal(
    report.entries.find((e) => e.file === "double.MILK2").reason,
    "double preset",
  );
  writeReport("2026-01-02.json", [measurement("test.MILK")]);
  generate(root, benchmarkDirectory, output);
  assert.throws(
    () => generate(root, benchmarkDirectory, output, "typo"),
    /unknown preset selection/,
  );
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}
console.log(
  "PASS: parser precedence/conditionals, comments, deterministic output, capability checks, budgets, invalid/missing benchmark selection failure, empty fallback build",
);

writeFixture(output, source);
