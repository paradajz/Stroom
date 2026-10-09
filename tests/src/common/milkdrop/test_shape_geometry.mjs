import { unityRunner, writeFixture } from "../support/unity.mjs";
const [output] = process.argv.slice(2);
// Frozen pre-hoisting implementation: compare full primitive order and attributes.
const reference = `static inline float reference_bound(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, isfinite(x) ? x : 0));
}

static MilkVertex reference_vertex(const float* f, unsigned index, unsigned sides)
{
    float t         = PRESET_TAU * index / sides;
    float angle     = t + f[MO_ANG] + PRESET_TAU / 8;
    float tex_angle = t + f[MO_TEX_ANG] + PRESET_TAU / 8;
    float radius    = reference_bound(f[MO_RAD], -4, 4) * MILK_VIEWPORT_HALF_HEIGHT;
    float zoom      = reference_bound(fabsf(f[MO_TEX_ZOOM]), .2f, 100);

    if (f[MO_TEX_ZOOM] < 0)
    {
        zoom = -zoom;
    }

    int textured = f[MO_TEXTURED] != 0;
    return (MilkVertex){ reference_bound(f[MO_X], -4, 5) * DISPLAY_WIDTH + radius * cosf(angle), reference_bound(f[MO_Y], -4, 5) * DISPLAY_HEIGHT - radius * sinf(angle), reference_bound(f[MO_R2], 0, 1) * 255, reference_bound(f[MO_G2], 0, 1) * 255, reference_bound(f[MO_B2], 0, 1) * 255, reference_bound(f[MO_A2], 0, 1), textured ? .5f + (.5f * MILK_VIEWPORT_ASPECT) * cosf(tex_angle) / zoom : 0, textured ? .5f + .5f * sinf(tex_angle) / zoom : 0 };
}

/**
 * @brief Draw the fill and outline of one custom shape instance.
 *
 * @param f Evaluated shape fields.
 * @param c Drawing sink.
 * @param wrap Nonzero to wrap feedback texture coordinates.
 */
static void reference_draw(const float* f, PresetCanvas* c, int wrap)
{
    unsigned sides = (unsigned)reference_bound(f[MO_SIDES], 3, 100);
    preset_blend(c, f[MO_ADDITIVE] != 0);
    MilkVertex center   = { reference_bound(f[MO_X], -4, 5) * DISPLAY_WIDTH, reference_bound(f[MO_Y], -4, 5) * DISPLAY_HEIGHT, reference_bound(f[MO_R], 0, 1) * 255, reference_bound(f[MO_G], 0, 1) * 255, reference_bound(f[MO_B], 0, 1) * 255, reference_bound(f[MO_A], 0, 1), .5f, .5f };
    MilkVertex previous = reference_vertex(f, 0, sides);

    for (unsigned j = 0; j < sides; ++j)
    {
        MilkVertex a = previous, b = reference_vertex(f, j + 1, sides);
        /* Save fill attributes before the outline overwrites colors/alpha. */
        previous               = b;
        MilkVertex triangle[3] = { center, a, b };

        if (center.a > 0 || a.a > 0)
        {
            milk_object_triangle(c, triangle, f[MO_TEXTURED] != 0, wrap);
        }

        if (f[MO_BORDER_A] > 0)
        {
            a.r = b.r = reference_bound(f[MO_BORDER_R], 0, 1) * 255;
            a.g = b.g = reference_bound(f[MO_BORDER_G], 0, 1) * 255;
            a.b = b.b = reference_bound(f[MO_BORDER_B], 0, 1) * 255;
            a.a = b.a       = reference_bound(f[MO_BORDER_A], 0, 1);
            unsigned copies = f[MO_THICK] != 0 ? 4 : 1;

            for (unsigned k = 0; k < copies; ++k)
            {
                MilkVertex u = a, v = b;
                u.x += k == 1 || k == 2;
                v.x += k == 1 || k == 2;
                u.y += k >= 2;
                v.y += k >= 2;
                milk_object_line(c, u, v);
            }
        }
    }
}
`;
const recorded = (code) =>
  code
    .replaceAll("milk_object_triangle(", "record_triangle(")
    .replaceAll("milk_object_line(", "record_line(")
    .replaceAll("preset_blend(", "record_blend(");
let code =
  `#include "milkdrop/preset.h"
#include "milkdrop/viewport.h"
#include "unity.h"
#include <math.h>
#include <string.h>

static MilkVertex vertices[2][1200];
static unsigned count[2], side;
static unsigned commands[2][1000], command_count[2];
static void record_blend(PresetCanvas* c,int additive){(void)c;commands[side][command_count[side]++]=8+additive;}
static void record_triangle(PresetCanvas* c,const MilkVertex* v,int textured,int wrap){(void)c;commands[side][command_count[side]++]=16+textured+wrap*2;memcpy(vertices[side]+count[side],v,3*sizeof(*v));count[side]+=3;}
static void record_line(PresetCanvas* c,MilkVertex a,MilkVertex b){(void)c;commands[side][command_count[side]++]=32;vertices[side][count[side]++]=a;vertices[side][count[side]++]=b;}
` +
  recorded(reference) +
  `
static const MilkObjectProgram shape_program = { .type = 0 };
const MilkProgram milk_programs[] = {{ .object_count = 1, .objects = &shape_program }};
void __wrap_preset_blend(PresetCanvas* canvas,int additive) {record_blend(canvas,additive);}
void __wrap_milk_object_triangle(PresetCanvas* canvas,const MilkVertex* v,int textured,int wrap) {record_triangle(canvas,v,textured,wrap);}
void __wrap_milk_object_line(PresetCanvas* canvas,MilkVertex a,MilkVertex b) {record_line(canvas,a,b);}
`;
code += `static void shape_equivalence(void){
const float edges[]={0,-0.0f,1,-1,1e-30f,1e30f,INFINITY,-INFINITY,NAN};
uint32_t rng=123;
for(unsigned trial=0;trial<1200;++trial){
float f[MILK_OBJECT_VARIABLES]={0};
for(unsigned i=0;i<MILK_OBJECT_VARIABLES;++i){rng=rng*1664525u+1013904223u;f[i]=((rng>>8)/16777216.0f)*4-1;}
f[MO_SIDES]=3+trial%98;f[MO_TEXTURED]=trial%2;f[MO_THICK]=trial%3==0;f[MO_BORDER_A]=trial%4?.7f:0;
const unsigned fields[]={MO_X,MO_Y,MO_RAD,MO_TEX_ZOOM,MO_R2,MO_A2,MO_BORDER_A,MO_ANG,MO_TEX_ANG};
if(trial%2) f[fields[(trial/2)%9]]=edges[(trial/18)%9];
PresetCanvas c={.opacity=1};count[0]=count[1]=command_count[0]=command_count[1]=0;
side=0;reference_draw(f,&c,trial%2);side=1;
Preset preset={0};preset.milk.objects[0].count=1;
memcpy(preset.milk.objects[0].cache.shape[0],f,MILK_OBJECT_FIELDS*sizeof(float));
preset.milk.frame[ML_WRAP]=trial%2;milk_objects_draw(&preset,&c);
TEST_ASSERT_EQUAL_UINT(count[0],count[1]);TEST_ASSERT_EQUAL_UINT(command_count[0],command_count[1]);
TEST_ASSERT_EQUAL_MEMORY(commands[0],commands[1],command_count[0]*sizeof(unsigned));
for(unsigned i=0;i<count[0];++i){float a[8],b[8];memcpy(a,&vertices[0][i],sizeof(a));memcpy(b,&vertices[1][i],sizeof(b));
for(unsigned j=0;j<8;++j)if(!(isnan(a[j])&&isnan(b[j])))TEST_ASSERT_EQUAL_MEMORY(a+j,b+j,sizeof(float));}
}}
`;
writeFixture(output, code + unityRunner("shape_equivalence"));
