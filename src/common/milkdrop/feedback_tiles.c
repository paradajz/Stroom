/* Split wrapped UV triangles at integer texture boundaries. A 640-wide GS
 * framebuffer cannot use power-of-two hardware repeat without sampling padding. */
#include "milkdrop/feedback.h"
#include "milkdrop/viewport.h"
#include <math.h>

/**
 * @brief Clip a textured polygon against one UV boundary.
 *
 * @param in Input polygon.
 * @param n Input vertex count.
 * @param out Destination with room for n + 1 vertices.
 * @param axis Zero for u, nonzero for v.
 * @param bound Texture boundary value.
 * @param greater Nonzero keeps values above the boundary; zero keeps below.
 * @return Number of output vertices.
 */
static unsigned clip(const FeedbackVertex* in, unsigned n, FeedbackVertex* out, unsigned axis, float bound, int greater)
{
    unsigned count = 0;

    for (unsigned i = 0; i < n; ++i)
    {
        FeedbackVertex a = in[i], b = in[i + 1 < n ? i + 1 : 0];
        float          av = axis ? a.v : a.u, bv = axis ? b.v : b.u;
        int            ia = greater ? av >= bound : av <= bound, ib = greater ? bv >= bound : bv <= bound;

        if (ia)
        {
            out[count++] = a;
        }

        if (ia != ib)
        {
            float t = (bound - av) / (bv - av);

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
static void emit(const FeedbackVertex* v, int wrap, FeedbackTriangle draw, void* ctx)
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

            unsigned n = 3;

            /* A triangle contained in one tile survives all four clip planes
             * unchanged. Still apply the same UV normalization and area test. */

            if (minx != maxx || miny != maxy)
            {
                n = clip(a, 3, b, 0, x, 1);

                if (!n)
                {
                    continue;
                }

                n = clip(b, n, a, 0, x + 1, 0);

                if (!n)
                {
                    continue;
                }

                n = clip(a, n, b, 1, y, 1);

                if (!n)
                {
                    continue;
                }

                n = clip(b, n, a, 1, y + 1, 0);

                if (!n)
                {
                    continue;
                }
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

void feedback_emit(const FeedbackMesh* mesh, FeedbackTriangle draw, void* context)
{
    for (unsigned y = 0; y < FEEDBACK_Y; ++y)
    {
        for (unsigned x = 0; x < FEEDBACK_X; ++x)
        {
            unsigned       n = y * (FEEDBACK_X + 1) + x, ids[] = { n, n + 1, n + FEEDBACK_X + 2, n + FEEDBACK_X + 1 };
            FeedbackVertex v[4];

            for (unsigned i = 0; i < 4; ++i)
            {
                v[i] = (FeedbackVertex){ (x + (i == 1 || i == 2)) * (float)DISPLAY_WIDTH / FEEDBACK_X,
                                         (y + (i >= 2)) * (float)DISPLAY_HEIGHT / FEEDBACK_Y,
                                         mesh->uv[ids[i]].u,
                                         mesh->uv[ids[i]].v };
            }

            FeedbackVertex a[3] = { v[0], v[1], v[2] }, b[3] = { v[0], v[2], v[3] };

            emit(a, mesh->wrap, draw, context);
            emit(b, mesh->wrap, draw, context);
        }
    }
}

void feedback_echo_emit(const MilkEcho* echo, FeedbackTriangle draw, void* context)
{
    FeedbackVertex v[4] = { { 0, 0, 0, 0 }, { DISPLAY_WIDTH, 0, 0, 0 }, { DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0 }, { 0, DISPLAY_HEIGHT, 0, 0 } };

    for (unsigned i = 0; i < 4; ++i)
    {
        milk_echo_uv(echo, v[i].x / DISPLAY_WIDTH, v[i].y / DISPLAY_HEIGHT, &v[i].u, &v[i].v);
    }

    FeedbackVertex a[3] = { v[0], v[1], v[3] }, b[3] = { v[1], v[2], v[3] };

    emit(a, echo->wrap ? 1 : 2, draw, context);
    emit(b, echo->wrap ? 1 : 2, draw, context);
}
