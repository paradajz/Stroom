import { unityRunner, writeFixture } from "../../support/unity.mjs";
import assert from "node:assert/strict";
import { Parser } from "../../../../../tools/milkdrop/compile_presets.mjs";
import {
  objectBuiltin,
  OBJECT_VARIABLES,
} from "../../../../../tools/milkdrop/preset_objects.mjs";
import { cachePointInvariants } from "../../../../../tools/milkdrop/point_invariants.mjs";
import { nestedTrigGuard } from "../../../../../tools/milkdrop/nested_trig.mjs";
const [output] = process.argv.slice(2);
for (const expression of [
  "milk_rand(rng,2.0f)",
  "milk_div(v[0],cosf(v[1]))",
  "(v[0]=cosf(v[1]))",
  "cosf(sinf(v[0]))",
  "(1e30f*cosf(v[0]))",
])
  assert.equal(nestedTrigGuard(expression), null);
assert(nestedTrigGuard("((v[0]-(5e-1f*v[1]))+(1e-1f*cosf((209.4f*v[2]))))"));
const fixtures = [
  "x=x+q1*(x-.5*value1+.1*cos(209.4367*y));",
  "y=y-q1*(value1+sin(sample*time)*cos(value2));",
  "x=x+q1*(-value1+.2*sin(sample));",
  "x=x+q1*(value1+sin(sample));x=x+q2*(x+.1*cos(x));",
  "x=x+q1*(rand(4)+cos(sample));",
  "x=x+q1*(value1+cos(rand(4)));",
  "x=x+q1*(value1+if(sample,cos(value1),rand(4)));",
  "x=x+q1*(value1+cos(sin(sample)));",
];
let source = `#include "milkdrop/milk.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>
static unsigned calls;
static float counted_sin(float x){++calls;return milk_fast_sin(x);}
static float counted_cos(float x){++calls;return milk_fast_cos(x);}
#define sinf counted_sin
#define cosf counted_cos
`;
const sizes = [];
fixtures.forEach((text, i) => {
  const vars = new Map(objectBuiltin.map((n, i) => [n, i]));
  const raw = new Parser(text, vars, { object: true, readonly: [] }).parse();
  sizes.push(vars.size);
  const optimized = cachePointInvariants(raw, vars, OBJECT_VARIABLES, false);
  assert.equal(optimized.includes("float nested_base="), i < 4, text);
  source += `static void old_${i}(float*v,uint32_t*rng){(void)rng;\n${raw}}\nstatic void new_${i}(float*v,uint32_t*rng){(void)rng;\n${optimized}\n}\n`;
});
source += `static void nested_trig_equivalence(void){unsigned saved=0;const float edge[]={0,-0.0f,.5f,-1,1e-30f,1048576,1048577,1e30f,INFINITY,-INFINITY,NAN};\n`;
fixtures.forEach((_, i) => {
  source += `{
for(unsigned mode=0;mode<4;++mode)for(unsigned e=0;e<11;++e)for(unsigned f=0;f<11;++f){
float a[MILK_OBJECT_VARIABLES]={0},b[MILK_OBJECT_VARIABLES]={0};uint32_t ra=17,rb=17;
a[MO_Q1]=b[MO_Q1]=mode&1;a[MO_Q1+1]=b[MO_Q1+1]=(mode>>1)&1;
a[MO_TIME]=b[MO_TIME]=edge[e];
for(unsigned j=0;j<17;++j){
a[MO_SAMPLE]=b[MO_SAMPLE]=j/16.0f;
a[MO_X]=b[MO_X]=edge[e];a[MO_Y]=b[MO_Y]=edge[f];
a[MO_VALUE1]=b[MO_VALUE1]=edge[f];a[MO_VALUE2]=b[MO_VALUE2]=edge[e];
calls=0;old_${i}(a,&ra);unsigned before=calls;
calls=0;new_${i}(b,&rb);TEST_ASSERT_EQUAL_UINT32(ra,rb);TEST_ASSERT_LESS_OR_EQUAL_UINT(before,calls);saved+=before-calls;
for(unsigned k=0;k<${sizes[i]};++k)if(memcmp(a+k,b+k,sizeof(float)) && !(isnan(a[k])&&isnan(b[k]))){
fprintf(stderr,"fixture ${i} mode %u edge %u/%u point %u slot %u: %a != %a\\n",mode,e,f,j,k,(double)a[k],(double)b[k]);TEST_FAIL_MESSAGE("Generated state differs; see fixture diagnostics");}
}}}
`;
});
source +=
  'TEST_ASSERT_GREATER_THAN_UINT(0,saved);printf("PASS: nested guards preserve full state and RNG; %u trig calls avoided\\n",saved);}';
source += unityRunner("nested_trig_equivalence");
writeFixture(output, source);
