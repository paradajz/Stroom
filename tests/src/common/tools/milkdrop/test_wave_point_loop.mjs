import { unityRunner, writeFixture } from "../../support/unity.mjs";
import fs from "node:fs";
import {
  loadLatestBenchmark,
  selectPlaybackPresets,
} from "../../../../../tools/milkdrop/benchmark_reports.mjs";
import path from "node:path";
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
  '#include "milkdrop/milk_wave_points.h"\n#include "unity.h"\n#include <string.h>\n#define sinf milk_fast_sin\n#define cosf milk_fast_cos\n';
source += `static float reference_bound(float x,float lo,float hi){return fminf(hi,fmaxf(lo,milk_finite(x)));}
static void reference_loop(MilkObjectState* s,const float data[2][MILK_OBJECT_SAMPLES],unsigned count,void (*point)(float*,uint32_t*)){
const float* f=s->frame;
    float* v = s->point;
    memcpy(v + MO_Q1, f + MO_Q1, MILK_OBJECT_POINT_REGISTERS * sizeof(float));

    for (unsigned j = 0; j < count; ++j)
    {
            v[MO_SAMPLE] = count > 1 ? (float)j / (count - 1) : 0;
        v[MO_VALUE1] = data[0][j];
        v[MO_VALUE2] = data[1][j];
        v[MO_X]      = .5f + data[0][j];
        v[MO_Y]      = .5f + data[1][j];
        memcpy(v + MO_R, f + MO_R, 4 * sizeof(float));
            point(v, &s->random);
            s->cache.wave[j] = (MilkVertex){ reference_bound(v[MO_X], -4, 5) * DISPLAY_WIDTH, MILK_VIEWPORT_HALF_HEIGHT + (reference_bound(v[MO_Y], -4, 5) - .5f) * DISPLAY_WIDTH, reference_bound(v[MO_R], 0, 1) * 255, reference_bound(v[MO_G], 0, 1) * 255, reference_bound(v[MO_B], 0, 1) * 255, reference_bound(v[MO_A], 0, 1), 0, 0 };
                    }

}
`;
for (let i = 0; i < cases.length; ++i) {
  const vars = new Map(objectBuiltin.map((n, i) => [n, i]));
  for (const stage of cases[i].slice(1))
    new Parser(stage, vars, { object: true, readonly: [] }).parse();
  const raw = new Parser(cases[i][0], vars, {
    object: true,
    readonly: [],
  }).parse();
  const code = cachePointInvariants(raw, vars, OBJECT_VARIABLES);
  source += `static __attribute__((noinline)) void scalar_${i}(float* v,uint32_t* rng){(void)v;(void)rng;\n${code}}\nMILK_WAVE_POINT_LOOP(loop_${i},\n${code})\n`;
}
source += `static void equivalence(void){
static MilkObjectState a,b;
static float data[2][MILK_OBJECT_SAMPLES];
const unsigned counts[]={0,1,2,17,511,512};
`;
for (let i = 0; i < cases.length; ++i)
  source += `
memset(&a,0,sizeof(a));
for(unsigned k=0;k<MILK_OBJECT_VARIABLES;++k)a.point[k]=((int)(k%13)-6)*.125f;
a.random=123;
for(unsigned frame=0;frame<12;++frame){
for(unsigned k=0;k<MILK_OBJECT_FIELDS;++k)a.frame[k]=((int)((k+frame)%17)-8)*.125f;
for(unsigned k=0;k<MILK_OBJECT_POINT_REGISTERS;++k)a.frame[MO_Q1+k]=((k+frame)%7)*.25f;
a.point[MO_TIME]=frame*1.25f;
for(unsigned j=0;j<512;++j){data[0][j]=sinf(j*.17f);data[1][j]=cosf(j*.23f);}
b=a;
unsigned count=counts[frame%6];
reference_loop(&a,data,count,scalar_${i});
loop_${i}(&b,data,count);
TEST_ASSERT_EQUAL_UINT32(a.random,b.random);
TEST_ASSERT_EQUAL_MEMORY(a.point,b.point,sizeof(a.point));
TEST_ASSERT_EQUAL_MEMORY(a.frame,b.frame,sizeof(a.frame));
if(count)TEST_ASSERT_EQUAL_MEMORY(a.cache.wave,b.cache.wave,count*sizeof(MilkVertex));
}
`;
source += "}\n";
writeFixture(output, source + unityRunner("equivalence"));
