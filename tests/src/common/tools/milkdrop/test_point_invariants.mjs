import { unityRunner, writeFixture } from "../../support/unity.mjs";
import fs from "node:fs";
import {
  loadLatestBenchmark,
  selectPlaybackPresets,
} from "../../../../../tools/milkdrop/benchmark_reports.mjs";
import path from "node:path";
import assert from "node:assert/strict";
import {
  Parser,
  parsePreset,
} from "../../../../../tools/milkdrop/compile_presets.mjs";
import {
  objectBuiltin,
  OBJECT_VARIABLES,
} from "../../../../../tools/milkdrop/preset_objects.mjs";
import { cachePointInvariants } from "../../../../../tools/milkdrop/point_invariants.mjs";
const [output, root, benchmarkDirectory] = process.argv.slice(2);
const cases = [
  ["x=x-(below(t1,1)*.05)*sin(sample*time);y=y-above(t2,0)*cos(value1);"],
  ["x=x+equal(t1,0)*cos(sample*time);y=y+equal(t2,1)*sin(value1);"],
  ["x=x+equal(t1,0)*cos(sample+value1);y=y+equal(t2,1)*sin(sample+value1);"],
  ["x=x-equal(t1,0)*sin(sample+value1);y=y+equal(t2,1)*cos(sample+value1);"],
  ["x=x+equal(t1,0)*cos(sample+x);y=y+equal(t2,1)*sin(sample+x);"],
  ["x=x+equal(t1,0)*cos(sample);y=y+x*sin(sample);"],
  ["x=x+equal(t1,0)*cos(sample);x=x+equal(t2,1)*sin(sample);"],
  ["x=x+rand(2)*cos(sample);y=y+equal(t1,0)*sin(rand(3));"],
  [
    "x=sample*(time*3+t1);y=equal(t1,2)*value1+sign(t2)*sample;a=if(sample,rand(3),time*2);",
  ],
  ["t1=t1+1;x=sample*(t1*3);t1=time;y=sample*(t1*3);"],
  ["t1=cos(sample*time);t2=sin(sample*time);x=t1;y=t2;"],
  ["x=sin(x);y=cos(x);t1=sin(rand(3));t2=cos(rand(3));"],
  ["ang=time*.3; x=sin(ang)+sample; ang=time*.7; y=cos(ang)+value1;"],
  [
    "t1=t1+sin(time);x=t1; y=rand(9)+sin(time); a=if(sample,rand(3),cos(time));",
  ],
  ["x=sin(x)+sin(time); r=r+sample; y=sin(r);"],
  ["x=sin(t1);t1=time; y=cos(t1);"],
  ["ang=sin(time);x=ang+sample;y=sin(time);a=if(0,rand(8),sin(time));"],
];
// Exercise every playback custom wave using its original variable allocation.
const { report } = loadLatestBenchmark(benchmarkDirectory);
for (const file of selectPlaybackPresets(report)) {
  const fields = parsePreset(fs.readFileSync(path.join(root, file), "latin1"));
  for (let i = 0; i < 4; ++i) {
    if (Number(fields.get(`wavecode_${i}_enabled`)) !== 1) continue;
    const stage = (name) =>
      [...fields]
        .filter(([k]) => new RegExp(`^wave_${i}_${name}\\d+$`).test(k))
        .sort(
          (a, b) =>
            Number(a[0].match(/\d+$/)[0]) - Number(b[0].match(/\d+$/)[0]),
        )
        .map(([, v]) => v)
        .join("\n");
    cases.push([stage("per_point"), stage("init"), stage("per_frame")]);
  }
}
let source =
  '#include "milkdrop/milk.h"\n#include "unity.h"\n#include <string.h>\n#include <stdio.h>\n#define sinf milk_fast_sin\n#define cosf milk_fast_cos\n';
const sizes = [];
for (let i = 0; i < cases.length; ++i) {
  const vars = new Map(objectBuiltin.map((n, i) => [n, i]));
  for (const stage of cases[i].slice(1))
    new Parser(stage, vars, { object: true, readonly: [] }).parse();
  const original = new Parser(cases[i][0], vars, {
    object: true,
    readonly: [],
  }).parse();
  sizes.push(vars.size);
  const baseline = cachePointInvariants(
    original,
    new Map(vars),
    OBJECT_VARIABLES,
    false,
  );
  const optimized = cachePointInvariants(original, vars, OBJECT_VARIABLES);
  assert(vars.size <= OBJECT_VARIABLES);
  if (i === 2 || i === 3) assert(optimized.includes("int skip_a="));
  if (i >= 4 && i <= 6) assert(!optimized.includes("int skip_a="));
  // A full variable pool must safely retain the unoptimized program.
  const full = new Map(vars);
  while (full.size < OBJECT_VARIABLES)
    full.set(`padding${full.size}`, full.size);
  assert.equal(
    cachePointInvariants(original, full, OBJECT_VARIABLES),
    original,
  );
  source += `static void old_${i}(float* v,uint32_t* rng){(void)v;(void)rng;\n${original}}\nstatic void base_${i}(float* v,uint32_t* rng){(void)v;(void)rng;\n${baseline}}\nstatic void new_${i}(float* v,uint32_t* rng){(void)v;(void)rng;\n${optimized}}\n`;
}
source += `static void point_invariant_equivalence(void){float a[MILK_OBJECT_VARIABLES],b[MILK_OBJECT_VARIABLES],c[MILK_OBJECT_VARIABLES];uint32_t r,s,t;\n`;
for (let i = 0; i < cases.length; ++i)
  source += `
for(unsigned frame=0;frame<12;++frame){
for(unsigned k=0;k<MILK_OBJECT_VARIABLES;++k)a[k]=b[k]=((int)((k*17+frame*13)%31)-15)*.125f;
r=s=123+frame;
a[MO_TIME]=b[MO_TIME]=frame*7.125f;
memcpy(c,a,sizeof(a)); t=r;
for(unsigned j=0;j<512;++j){
a[MO_SAMPLE]=b[MO_SAMPLE]=j/511.0f;
a[MO_VALUE1]=b[MO_VALUE1]=sinf(j*.17f);a[MO_VALUE2]=b[MO_VALUE2]=cosf(j*.23f);
a[MO_X]=b[MO_X]=.5f;a[MO_Y]=b[MO_Y]=.5f;
for(unsigned k=MO_R;k<=MO_A;++k)a[k]=b[k]=.25f;
for(unsigned k=0;k<${sizes[i]};++k)c[k]=a[k];
old_${i}(a,&r);base_${i}(c,&t);new_${i}(b,&s);TEST_ASSERT_EQUAL_UINT32(r,s);TEST_ASSERT_EQUAL_UINT32(r,t);
for(unsigned k=0;k<${sizes[i]};++k)TEST_ASSERT_TRUE_MESSAGE(!memcmp(a+k,c+k,sizeof(float)) || (isnan(a[k])&&isnan(c[k])), "Baseline float bits differ");
for(unsigned k=0;k<${sizes[i]};++k)if((j==0 || j==511 || k==MO_X || k==MO_Y || (k>=MO_R && k<=MO_A)) && memcmp(a+k,b+k,sizeof(float)) && !(isnan(a[k])&&isnan(b[k]))){fprintf(stderr,"case ${i} frame %u point %u slot %u: %a vs %a\\n",frame,j,k,(double)a[k],(double)b[k]);TEST_FAIL_MESSAGE("Generated state differs; see fixture diagnostics");}
}}
`;
for (const edgeCase of [0, 1, 2, 3, 4, 5, 6])
  source += `{
const float edge[] = { 0.0f, -0.0f, 1.0f, -1.0f, 1e-30f, 1e30f, INFINITY, -INFINITY, NAN };
for(unsigned mode=0;mode<4;++mode)for(unsigned i=0;i<9;++i)for(unsigned j=0;j<9;++j){
memset(a,0,sizeof(a));memset(b,0,sizeof(b));r=s=17;
a[MO_X]=b[MO_X]=edge[i];a[MO_Y]=b[MO_Y]=edge[i];
a[MO_TIME]=b[MO_TIME]=edge[j];a[MO_VALUE1]=b[MO_VALUE1]=edge[j];
a[MO_T1]=b[MO_T1]= (mode % 2)*2;
a[MO_T2]=b[MO_T2]= (mode / 2);
old_${edgeCase}(a,&r);new_${edgeCase}(b,&s);
for(unsigned k=0;k<${sizes[edgeCase]};++k)TEST_ASSERT_TRUE_MESSAGE(!memcmp(a+k,b+k,sizeof(float)) || (isnan(a[k])&&isnan(b[k])), "Optimized float bits differ");
}}
`;
source +=
  'puts("PASS: baseline full state, deferred rendered outputs, final state and RNG match");}';
source += unityRunner("point_invariant_equivalence");
writeFixture(output, source);
