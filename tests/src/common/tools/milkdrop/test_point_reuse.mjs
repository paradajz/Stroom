import assert from "node:assert/strict";
import fs from "node:fs";
import { unityRunner, writeFixture } from "../../support/unity.mjs";
import { Parser } from "../../../../../tools/milkdrop/compile_presets.mjs";
import {
  objectBuiltin,
  OBJECT_VARIABLES,
} from "../../../../../tools/milkdrop/preset_objects.mjs";
import { cachePointInvariants } from "../../../../../tools/milkdrop/point_invariants.mjs";
import { cachePointDecisions } from "../../../../../tools/milkdrop/point_decisions.mjs";
import { reusePointTrig } from "../../../../../tools/milkdrop/point_reuse.mjs";
const [output] = process.argv.slice(2);
const fixtures = [
  "x=sin(sample)+sin(sample);y=sin(sample);",
  "x=cos(sample*time);r=1;y=cos(sample*time);",
  "x=sin(sample);sample=sample+1;y=sin(sample);",
  "sample=sin(sample);y=sin(sample);",
  "x=if(q1,sin(sample),0);y=sin(sample);",
  "x=sin(sample);y=if(q1,sin(sample),0);",
  "x=sin(rand(5));y=sin(rand(5));",
  "x=sin(sample);q1=rand(3);y=sin(sample);",
  "x=sin(cos(sample));y=sin(cos(sample));",
  "x=sin(sample*time)+cos(sample*time);y=sin(sample*time);",
  "x=cos(sample*time);r=1;y=sin(sample*time)+cos(sample*time);",
  "x=sin(sample)+sin(sample);sample=sample+1;y=cos(sample);",
  "x=sin(sample)+sin(sample);y=if(q1,cos(sample),0);",
  "x=sin(sample)+sin(sample);q1=rand(3);y=cos(sample);",
  "x=sin(sample)+sin(sample);y=cos(time);",
  "sample=sin(sample)+sin(sample);y=cos(sample);",
];
fixtures.push(
  "x=x+above(sample,.5)*sin(value1)*value2;y=y+above(sample,.5)*cos(value1)*value2;",
  "x=.5+above(sample,.5)*sin(value1)*value2*q1;y=.5+above(sample,.5)*cos(value1)*value2;",
  "x=x+above(sample,.5)*sin(value1)*value2;y=x+above(sample,.5)*cos(value1)*value2;",
  "value1=value1+above(sample,.5)*sin(value1)*value2;y=y+above(sample,.5)*cos(value1)*value2;",
  "x=x+above(sample,.5)*sin(value1)*rand(2);y=y+above(sample,.5)*cos(value1)*value2;",
);
fixtures.push(
  "phase=if(above(sample,0),phase,phase+time);x=.5+above(sample,.5)*sin(phase)*q1;y=.5+above(sample,.5)*cos(phase)*q1;",
  "phase=if(above(sample,0),phase,phase+time);phase=phase+value1;x=.5+above(sample,.5)*sin(phase)*q1;y=.5+above(sample,.5)*cos(phase)*q1;",
  "value1=if(above(sample,0),value1,time);x=.5+above(sample,.5)*sin(value1)*q1;y=.5+above(sample,.5)*cos(value1)*q1;",
  "sample=sample+.1;phase=if(above(sample,0),phase,time);x=.5+above(sample,.5)*sin(phase)*q1;y=.5+above(sample,.5)*cos(phase)*q1;",
);
const decision =
  "if(equal(n%2,0),1,if(equal((n+1)%4,0),2,if(equal((n+3)%8,0),3,4)))";
const decisionStart = fixtures.length;
fixtures.push(
  `n=if(above(sample,0),n+equal(q1,1),time);choice=${decision};x=choice;`,
  `n=value1;choice=${decision};x=choice;`,
  `n=sample;choice=${decision};choice=rand(2);`,
  `sample=sample+.1;n=time;choice=${decision};`,
  `n=${decision};x=n;`,
  `n=time;choice=${decision.replace("3,4", "rand(2),4")};`,
  `n=value1;choice=${decision.replace("3,4", "value2,4")};`,
);
for (const name of [
  "shifter - digi",
  "shifter - robotopia",
  "shifter - fractal grinder",
]) {
  const text = fs.readFileSync(
    new URL(
      `../../../../../third_party/presets-milkdrop-original/Milkdrop-Original/${name}.milk`,
      import.meta.url,
    ),
    "utf8",
  );
  for (let wave = 0; wave < 4; ++wave) {
    if (
      !new RegExp(`^wavecode_${wave}_enabled=1$`, "m").test(
        text.replaceAll("\r", ""),
      )
    )
      continue;
    fixtures.push(
      text
        .split(/\r?\n/)
        .filter((line) => new RegExp(`^wave_${wave}_per_point\\d+=`).test(line))
        .map((line) => line.slice(line.indexOf("=") + 1))
        .join("\n"),
    );
  }
}
let source = `#include "milkdrop/milk.h"
#include "unity.h"
#include <string.h>

#define sinf counted_sin
#define cosf counted_cos
#define milk_fast_sincos(x,s,c) counted_sincos(x,s,c)

static unsigned calls;
static float counted_sin(float x){++calls;return milk_fast_sin(x);}
static float counted_cos(float x){++calls;return milk_fast_cos(x);}
static void counted_sincos(float x,float*s,float*c){calls+=2;(milk_fast_sincos)(x,s,c);}
`;
const programs = fixtures.map((text) => {
  const vars = new Map(objectBuiltin.map((name, i) => [name, i]));
  const raw = new Parser(text, vars, { object: true, readonly: [] }).parse();
  const size = vars.size;
  return {
    raw,
    size,
    reused: reusePointTrig(raw),
    pipeline: cachePointInvariants(raw, vars, OBJECT_VARIABLES, false),
  };
});
assert(programs[0].reused.includes("point_trig_"));
assert(programs[16].pipeline.includes("int skip_a"));
assert(!programs[18].pipeline.includes("int skip_a"));
assert(!programs[19].pipeline.includes("int skip_a"));
for (const i of [2, 3, 4, 5, 6, 7])
  assert.equal(programs[i].reused, programs[i].raw);
for (const i of [9, 10])
  assert(programs[i].reused.includes("milk_fast_sincos("));
for (const i of [11, 12, 13, 14, 15])
  assert(!programs[i].reused.includes("milk_fast_sincos("));
assert(programs[21].pipeline.includes("if (v[79] == 0) milk_fast_sincos"));
for (const i of [22, 23, 24])
  assert(!programs[i].pipeline.includes("if (v[79] == 0) milk_fast_sincos"));
for (const i of [0, 1, 2])
  assert(programs[decisionStart + i].pipeline.includes(" != 0 && v["));
for (const i of [3, 4, 5, 6])
  assert(!programs[decisionStart + i].pipeline.includes(" != 0 && v["));
{
  const vars = new Map(objectBuiltin.map((name, i) => [name, i]));
  const raw = new Parser(fixtures[decisionStart], vars, {
    object: true,
    readonly: [],
  })
    .parse()
    .split("\n");
  const size = vars.size;
  assert.deepEqual(cachePointDecisions(raw, vars, size + 1), raw);
  assert.equal(vars.size, size);
  const missingSample = new Map(vars);
  missingSample.delete("sample");
  assert.deepEqual(
    cachePointDecisions(raw, missingSample, OBJECT_VARIABLES),
    raw,
  );
}
programs.forEach((p, i) => {
  for (const kind of ["raw", "reused", "pipeline"])
    source += `static void ${kind}_${i}(float*v,uint32_t*rng){(void)v;(void)rng;\n${p[kind]}\n}\n`;
});
source += `static void point_reuse_equivalence(void){unsigned saved=0;
const float edges[]={0,-0.0f,1,-1,1e-30f,1e30f,1048576,1048577,INFINITY,-INFINITY,NAN};
`;
programs.forEach((p, i) => {
  source += `for(unsigned mode=0;mode<11;++mode){
float a[MILK_OBJECT_VARIABLES]={0},b[MILK_OBJECT_VARIABLES]={0},c[MILK_OBJECT_VARIABLES]={0};uint32_t ra=123,rb=123,rc=123;
for(unsigned k=0;k<${p.size};++k)a[k]=b[k]=c[k]=(float)(k%7)*.125f;
a[MO_TIME]=b[MO_TIME]=c[MO_TIME]=edges[mode];
a[MO_Q1]=b[MO_Q1]=c[MO_Q1]=mode&1;
a[MO_X]=b[MO_X]=c[MO_X]=edges[mode];
a[MO_Y]=b[MO_Y]=c[MO_Y]=edges[(mode+2)%11];
a[MO_VALUE1]=b[MO_VALUE1]=c[MO_VALUE1]=edges[mode];
a[MO_VALUE2]=b[MO_VALUE2]=c[MO_VALUE2]=edges[(mode+1)%11];
for(unsigned wave=0;wave<3;++wave)for(unsigned point=0;point<512;++point){
a[MO_SAMPLE]=b[MO_SAMPLE]=c[MO_SAMPLE]=(float)point/511;
calls=0;raw_${i}(a,&ra);unsigned before=calls;
calls=0;reused_${i}(b,&rb);TEST_ASSERT_LESS_OR_EQUAL_UINT(before,calls);saved+=before-calls;
pipeline_${i}(c,&rc);
TEST_ASSERT_EQUAL_UINT32(ra,rb);TEST_ASSERT_EQUAL_UINT32(ra,rc);
for(unsigned k=0;k<${p.size};++k){
if(!(isnan(a[k])&&isnan(b[k])))TEST_ASSERT_EQUAL_MEMORY(a+k,b+k,sizeof(float));
if(!(isnan(a[k])&&isnan(c[k])))TEST_ASSERT_EQUAL_MEMORY(a+k,c+k,sizeof(float));
}}}
`;
});
source += "TEST_ASSERT_GREATER_THAN_UINT(0,saved);}";
source += unityRunner("point_reuse_equivalence");
writeFixture(output, source);
