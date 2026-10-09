import { unityRunner, writeFixture } from "../support/unity.mjs";
const [output] = process.argv.slice(2);
// Frozen original splitter preserves boundary handling and emitted triangle order.
const reference = `static unsigned reference_clip(const FeedbackVertex* in, unsigned n, FeedbackVertex* out, unsigned axis, float bound, int greater)
{
    unsigned count = 0;

    for (unsigned i = 0; i < n; ++i)
    {
        FeedbackVertex a = in[i], b = in[(i + 1) % n];
        float          av = axis ? a.v : a.u, bv = axis ? b.v : b.u;
        int            ia = greater ? av >= bound : av <= bound, ib = greater ? bv >= bound : bv <= bound;

        if (ia)
        {
            out[count++] = a;
        }

        if (ia != ib)
        {
            float t      = (bound - av) / (bv - av);
            out[count++] = (FeedbackVertex){ a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.u + t * (b.u - a.u), a.v + t * (b.v - a.v) };
        }
    }

    return count;
}

/**
 * @brief Split a triangle at texture boundaries and emit nondegenerate pieces.
 *
 * @param v Three source vertices.
 * @param wrap 0 passes through, 1 wraps tiles, 2 splits clamped edges.
 * @param draw Triangle callback.
 * @param ctx Opaque callback context.
 */
static void reference_emit(const FeedbackVertex* v, int wrap, FeedbackTriangle draw, void* ctx)
{
    if (!wrap)
    {
        draw(ctx, v);
        return;
    }

    int minx = (int)floorf(fminf(v[0].u, fminf(v[1].u, v[2].u))), maxx = (int)floorf(fmaxf(v[0].u, fmaxf(v[1].u, v[2].u)));
    int miny = (int)floorf(fminf(v[0].v, fminf(v[1].v, v[2].v))), maxy = (int)floorf(fmaxf(v[0].v, fmaxf(v[1].v, v[2].v)));

    for (int y = miny; y <= maxy; ++y)
    {
        for (int x = minx; x <= maxx; ++x)
        {
            FeedbackVertex a[12], b[12];

            for (unsigned i = 0; i < 3; ++i)
            {
                a[i] = v[i];
            }

            unsigned n = reference_clip(a, 3, b, 0, x, 1);

            if (!n)
            {
                continue;
            }

            n = reference_clip(b, n, a, 0, x + 1, 0);

            if (!n)
            {
                continue;
            }

            n = reference_clip(a, n, b, 1, y, 1);

            if (!n)
            {
                continue;
            }

            n = reference_clip(b, n, a, 1, y + 1, 0);

            if (!n)
            {
                continue;
            }

            for (unsigned i = 0; i < n; ++i)
            {
                a[i].u = fminf(1, fmaxf(0, a[i].u - (wrap == 1 ? x : 0)));
                a[i].v = fminf(1, fmaxf(0, a[i].v - (wrap == 1 ? y : 0)));
            }

            for (unsigned i = 1; i + 1 < n; ++i)
            {
                FeedbackVertex t[3] = { a[0], a[i], a[i + 1] };
                float          area = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);

                if (fabsf(area) > .0001f)
                {
                    draw(ctx, t);
                }
            }
        }
    }
}
`;
const code =
  `#include "milkdrop/feedback.h"
#include "milkdrop/viewport.h"
#include "unity.h"
#include <math.h>
#include <string.h>

static FeedbackVertex triangles[2][200000];
static unsigned counts[2], side;
static void record(void* context,const FeedbackVertex* v){(void)context;TEST_ASSERT_LESS_OR_EQUAL_UINT(199997,counts[side]);memcpy(triangles[side]+counts[side],v,3*sizeof(*v));counts[side]+=3;}
` +
  reference +
  `
static void reference_mesh(const FeedbackMesh* mesh) {
 for(unsigned y=0;y<FEEDBACK_Y;++y)for(unsigned x=0;x<FEEDBACK_X;++x){
  unsigned n=y*(FEEDBACK_X+1)+x,ids[]={n,n+1,n+FEEDBACK_X+2,n+FEEDBACK_X+1};
  FeedbackVertex v[4];
  for(unsigned i=0;i<4;++i)v[i]=(FeedbackVertex){(x+(i==1||i==2))*(float)DISPLAY_WIDTH/FEEDBACK_X,(y+(i>=2))*(float)DISPLAY_HEIGHT/FEEDBACK_Y,mesh->uv[ids[i]].u,mesh->uv[ids[i]].v};
  FeedbackVertex a[]={v[0],v[1],v[2]},b[]={v[0],v[2],v[3]};
  reference_emit(a,mesh->wrap,record,NULL);reference_emit(b,mesh->wrap,record,NULL);
 }
}
static void clipping_equivalence(void){
 uint32_t rng=123;const float edges[]={-2,-1,-0.0f,0,1,2,3};
 for(unsigned trial=0;trial<120;++trial){
  FeedbackMesh mesh={0};
  for(unsigned i=0;i<FEEDBACK_VERTICES;++i){
   float r[2];for(unsigned k=0;k<2;++k){rng=rng*1664525u+1013904223u;r[k]=(rng>>8)/16777216.0f;}
   mesh.uv[i]=(FeedbackUV){r[0]*5-2,r[1]*5-2};
   if(trial%4==0)mesh.uv[i]=(FeedbackUV){(int)(trial%5)-2+r[0]*.75f,(int)(trial%7)-3+r[1]*.75f};
   if(trial%4==1)mesh.uv[i]=(FeedbackUV){edges[(trial+i)%7],edges[(trial/7+i)%7]};
   if(trial%4==2)mesh.uv[i]=(FeedbackUV){nextafterf(edges[(trial+i)%7],i%2?INFINITY:-INFINITY),nextafterf(edges[(trial/7+i)%7],i%2?-INFINITY:INFINITY)};
   if(trial%11==0 && i)mesh.uv[i]=mesh.uv[i-1];
  }
  for(int wrap=0;wrap<3;++wrap){mesh.wrap=wrap;counts[0]=counts[1]=0;side=0;reference_mesh(&mesh);side=1;feedback_emit(&mesh,record,NULL);TEST_ASSERT_EQUAL_UINT(counts[0],counts[1]);if(counts[0])TEST_ASSERT_EQUAL_MEMORY(triangles[0],triangles[1],counts[0]*sizeof(FeedbackVertex));}
 }
}
`;
writeFixture(output, code + unityRunner("clipping_equivalence"));
