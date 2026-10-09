import { unityRunner, writeFixture } from "../../support/unity.mjs";
import assert from "node:assert/strict";
import { Parser } from "../../../../../tools/milkdrop/compile_presets.mjs";
import { objectBuiltin } from "../../../../../tools/milkdrop/preset_objects.mjs";
import { deferInactiveTrig } from "../../../../../tools/milkdrop/deferred_trig.mjs";
const [output] = process.argv.slice(2);
function compile(text) {
  const vars = new Map(objectBuiltin.map((n, i) => [n, i]));
  const raw = new Parser(text, vars, { object: true, readonly: [] }).parse();
  return { vars, raw, deferred: deferInactiveTrig(raw.split("\n"), vars) };
}
for (const text of [
  "tmp=sin(sample);x=tmp;", // unguarded read
  "x=x+q1*tmp;tmp=sin(sample);y=y+q1*tmp;", // old state read
  "tmp=sin(sample);tmp=cos(sample);x=x+q1*tmp;", // multiple writes
  "tmp=sin(rand(5));x=x+q1*tmp;", // RNG
  "x=sin(sample);y=y+q1*x;", // visible destination
  "tmp=sin(sample);x=x+tmp*tmp;", // candidate in scale
])
  assert.equal(compile(text).deferred, null, text);
const dependency = compile(
  "tmp=sin(sample);other=cos(tmp);x=x+q1*tmp;y=y+q1*other;",
);
assert(
  dependency.deferred.includes(
    `    (void)(v[${dependency.vars.get("tmp")}]=milk_finite(sinf(v[${dependency.vars.get("sample")}])));`,
  ),
);
const fixtures = [
  "tmp=sin(sample);other=cos(tmp);x=x+q1*tmp;y=y+q1*other;",
  "tmp=sin(sample*time);x=x+q1*tmp;",
  "c=cos(sample*time);s=sin(sample*time);x=x+q1*c;y=y-q2*s;",
  "p=sin(sample*time);n=cos(sample*time);x=x+q1*(p-n);y=y+q2*(p+n);",
  "tmp=sin(sample*time);time=time+1;x=x+q1*tmp;y=y+q2*tmp;", // angle snapshot
  "tmp=sin(sample*time);x=x+q1*tmp;sample=.5;", // driver sample captured at entry
];
let source = `#include "milkdrop/milk.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>
static unsigned calls;
static float counted_sin(float x){++calls;return milk_fast_sin(x);}
static float counted_cos(float x){++calls;return milk_fast_cos(x);}
static void counted_pair(float x,float*s,float*c){calls+=2;milk_fast_sincos(x,s,c);}
#define sinf counted_sin
#define cosf counted_cos
#define milk_fast_sincos counted_pair
`;
const programs = fixtures.map(compile);
programs.forEach((p, i) => {
  assert(p.deferred, fixtures[i]);
  source += `static void old_${i}(float*v,uint32_t*rng){(void)rng;\n${p.raw}}\nstatic void new_${i}(float*v,uint32_t*rng){(void)rng;\n${p.deferred.join("\n")}\n}\n`;
});
source += "static void deferred_trig_equivalence(void){unsigned saved=0;\n";
programs.forEach((p, i) => {
  source += `{
const float edges[]={0,-0.0f,1,-1,1e-30f,1e30f,1048576,1048577,INFINITY,-INFINITY,NAN};
const unsigned counts[]={1,2,7,512};
for(unsigned count=0;count<4;++count)for(unsigned mode=0;mode<4;++mode)
for(unsigned e=0;e<11;++e)for(unsigned b=0;b<11;++b){
float a[192]={0},v[192]={0};uint32_t ra=17,rv=17;
a[MO_TIME]=v[MO_TIME]=edges[e];a[MO_Q1]=v[MO_Q1]=mode&1;a[MO_Q1+1]=v[MO_Q1+1]=(mode>>1)&1;
for(unsigned j=0;j<counts[count];++j){
a[MO_SAMPLE]=v[MO_SAMPLE]=counts[count]>1?(float)j/(counts[count]-1):0;
a[MO_X]=v[MO_X]=edges[b];a[MO_Y]=v[MO_Y]=edges[b];
calls=0;old_${i}(a,&ra);unsigned before=calls;
calls=0;new_${i}(v,&rv);TEST_ASSERT_EQUAL_UINT32(ra,rv);TEST_ASSERT_LESS_OR_EQUAL_UINT(before,calls);saved+=before-calls;
for(unsigned k=0;k<${p.vars.size};++k)
if(j==0 || j+1==counts[count] || k==MO_X || k==MO_Y || (k>=MO_R && k<=MO_A))
if(memcmp(a+k,v+k,sizeof(float)) && !(isnan(a[k])&&isnan(v[k]))){
fprintf(stderr,"fixture ${i}, count %u mode %u angle %u base %u point %u slot %u: %a != %a\\n",counts[count],mode,e,b,j,k,(double)a[k],(double)v[k]);TEST_FAIL_MESSAGE("Generated state differs; see fixture diagnostics");}
}}}
`;
});
source +=
  'TEST_ASSERT_GREATER_THAN_UINT(0,saved);printf("PASS: generic deferred trig outputs and final state match; %u trig results avoided\\n",saved);}';
source += unityRunner("deferred_trig_equivalence");
writeFixture(output, source);
