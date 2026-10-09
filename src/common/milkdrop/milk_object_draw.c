/* Attribute-preserving clipping for custom shapes and waves. */
#include "milkdrop/preset.h"
#include "milkdrop/viewport.h"
#include <math.h>

#define LERP(k) (a.k += (b.k - a.k) * t)

/**
 * @brief Interpolate all position, color, opacity, and texture attributes.
 *
 * @param a First vertex.
 * @param b Second vertex.
 * @param t Interpolation fraction.
 * @return Interpolated vertex.
 */
static MilkVertex lerp(MilkVertex a, MilkVertex b, float t)
{
    LERP(x);
    LERP(y);
    LERP(r);
    LERP(g);
    LERP(b);
    LERP(a);
    LERP(u);
    LERP(v);

    return a;
}

/**
 * @brief Select a vertex coordinate for clipping.
 *
 * @param v Vertex to inspect.
 * @param n Coordinate index: 0 x, 1 y, 2 u, otherwise v.
 * @return Selected coordinate.
 */
static float axis(MilkVertex v, unsigned n)
{
    return n == 0 ? v.x : n == 1 ? v.y
                      : n == 2   ? v.u
                                 : v.v;
}

/**
 * @brief Clip a polygon against one coordinate boundary, preserving attributes.
 *
 * @param in Input polygon.
 * @param count Input vertex count.
 * @param out Destination with room for count + 1 vertices.
 * @param dim Coordinate index: 0 x, 1 y, 2 u, 3 v.
 * @param limit Boundary value.
 * @param greater Nonzero keeps values above the boundary; zero keeps below.
 * @return Number of output vertices.
 */
static unsigned clip(const MilkVertex* in, unsigned count, MilkVertex* out, unsigned dim, float limit, int greater)
{
    unsigned n = 0;

    for (unsigned i = 0; i < count; ++i)
    {
        MilkVertex a = in[i], b = in[(i + 1) % count];
        float      av = axis(a, dim), bv = axis(b, dim);
        int        inside = greater ? av >= limit : av <= limit, next = greater ? bv >= limit : bv <= limit;

        if (inside)
        {
            out[n++] = a;
        }

        if (inside != next)
        {
            out[n++] = lerp(a, b, (limit - av) / (bv - av));
        }
    }

    return n;
}

/**
 * @brief Triangulate a clipped polygon and apply opacity and texture mapping.
 *
 * @param c Drawing sink.
 * @param p Mutable polygon vertices.
 * @param n Polygon vertex count.
 * @param textured Nonzero for textured drawing.
 * @param wrap Nonzero for repeating texture coordinates.
 * @param x Horizontal texture tile index.
 * @param y Vertical texture tile index.
 */
static void emit(PresetCanvas* c, MilkVertex* p, unsigned n, int textured, int wrap, int x, int y)
{
    for (unsigned i = 0; i < n; ++i)
    {
        p[i].a *= c->opacity;

        if (textured)
        {
            p[i].u = fminf(1, fmaxf(0, p[i].u - (wrap ? x : 0)));
            p[i].v = fminf(1, fmaxf(0, p[i].v - (wrap ? y : 0)));
        }
    }

    for (unsigned i = 1; i + 1 < n; ++i)
    {
        MilkVertex t[3] = { p[0], p[i], p[i + 1] };

        c->object_triangle(c->context, t, textured);
    }
}

void milk_object_triangle(PresetCanvas* canvas, const MilkVertex* vertices, int textured, int wrap)
{
    if (!canvas->object_triangle)
    {
        return;
    }

    MilkVertex a[20], b[20];

    for (unsigned i = 0; i < 3; ++i)
    {
        a[i] = vertices[i];
    }

    unsigned n = 3;

    for (unsigned plane = 0; plane < 4 && n; ++plane)
    {
        unsigned dim   = plane / 2;
        float    limit = (plane & 1) ? (dim ? DISPLAY_HEIGHT : DISPLAY_WIDTH) : 0;

        n = clip(a, n, b, dim, limit, !(plane & 1));

        for (unsigned i = 0; i < n; ++i)
        {
            a[i] = b[i];
        }
    }

    if (n < 3)
    {
        return;
    }

    if (!textured)
    {
        emit(canvas, a, n, 0, 0, 0, 0);
        return;
    }

    float min_u = 3, max_u = -2, min_v = 3, max_v = -2;

    for (unsigned i = 0; i < n; ++i)
    {
        a[i].u = fminf(3, fmaxf(-2, a[i].u));
        a[i].v = fminf(3, fmaxf(-2, a[i].v));
        min_u  = fminf(min_u, a[i].u);
        max_u  = fmaxf(max_u, a[i].u);
        min_v  = fminf(min_v, a[i].v);
        max_v  = fmaxf(max_v, a[i].v);
    }

    for (int y = (int)floorf(min_v); y <= (int)floorf(max_v); ++y)
    {
        for (int x = (int)floorf(min_u); x <= (int)floorf(max_u); ++x)
        {
            MilkVertex p[20], q[20];

            for (unsigned i = 0; i < n; ++i)
            {
                p[i] = a[i];
            }

            unsigned m = clip(p, n, q, 2, x, 1);

            m = clip(q, m, p, 2, x + 1, 0);
            m = clip(p, m, q, 3, y, 1);
            m = clip(q, m, p, 3, y + 1, 0);

            if (m >= 3)
            {
                emit(canvas, p, m, 1, wrap, x, y);
            }
        }
    }
}

void milk_object_line(PresetCanvas* canvas, MilkVertex a, MilkVertex b)
{
    if (!canvas->object_line)
    {
        return;
    }

    float t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y;

    /* Most waveform segments are fully visible. Avoid four divisions and
     * min/max calls, but retain endpoint lerp below for identical rounding. */
    int inside = a.x >= 0 && a.x <= DISPLAY_WIDTH && a.y >= 0 && a.y <= DISPLAY_HEIGHT && b.x >= 0 && b.x <= DISPLAY_WIDTH && b.y >= 0 && b.y <= DISPLAY_HEIGHT;

    if (!inside)
    {
        const float p[4] = { -dx, dx, -dy, dy }, q[4] = { a.x, DISPLAY_WIDTH - a.x, a.y, DISPLAY_HEIGHT - a.y };

        for (unsigned i = 0; i < 4; ++i)
        {
            if (p[i] == 0)
            {
                if (q[i] < 0)
                {
                    return;
                }

                continue;
            }

            float t = q[i] / p[i];

            if (p[i] < 0)
            {
                t0 = fmaxf(t0, t);
            }
            else
            {
                t1 = fminf(t1, t);
            }

            if (t0 > t1)
            {
                return;
            }
        }
    }

    MilkVertex start = lerp(a, b, t0), end = lerp(a, b, t1);

    start.a *= canvas->opacity;
    end.a *= canvas->opacity;

    canvas->object_line(canvas->context, start, end);
}

void milk_object_wave_segment(PresetCanvas* canvas, MilkVertex a, MilkVertex mid, MilkVertex b, unsigned copies)
{
    if (!canvas->object_line)
    {
        return;
    }

    float margin = copies == 4 ? 1.0f : 0.0f;
    int   inside = a.x >= 0 && a.x <= DISPLAY_WIDTH - margin && a.y >= 0 && a.y <= DISPLAY_HEIGHT - margin &&
                   mid.x >= 0 && mid.x <= DISPLAY_WIDTH - margin && mid.y >= 0 && mid.y <= DISPLAY_HEIGHT - margin &&
                   b.x >= 0 && b.x <= DISPLAY_WIDTH - margin && b.y >= 0 && b.y <= DISPLAY_HEIGHT - margin;

    if (inside && canvas->object_wave_segments)
    {
        canvas->object_wave_segments(canvas->context, &a, &mid, &b, canvas->opacity, copies);
        return;
    }

    MilkVertex first[2], last[2];

    if (inside)
    {
        first[0] = lerp(a, mid, 0);
        last[0]  = lerp(a, mid, 1);
        first[1] = lerp(mid, b, 0);
        last[1]  = lerp(mid, b, 1);

        for (unsigned i = 0; i < 2; ++i)
        {
            first[i].a *= canvas->opacity;
            last[i].a *= canvas->opacity;
        }
    }

    for (unsigned k = 0; k < copies; ++k)
    {
        MilkVertex u = a, m = mid, w = b;
        float      dx = k == 1 || k == 2, dy = k >= 2;

        u.x += dx;
        m.x += dx;
        w.x += dx;
        u.y += dy;
        m.y += dy;
        w.y += dy;

        if (!inside)
        {
            milk_object_line(canvas, u, m);
            milk_object_line(canvas, m, w);
            continue;
        }

        const MilkVertex starts[2] = { u, m }, ends[2] = { m, w };

        for (unsigned i = 0; i < 2; ++i)
        {
            MilkVertex start = first[i], end = last[i];

            start.x = starts[i].x + (ends[i].x - starts[i].x) * 0.0f;
            start.y = starts[i].y + (ends[i].y - starts[i].y) * 0.0f;
            end.x   = starts[i].x + (ends[i].x - starts[i].x) * 1.0f;
            end.y   = starts[i].y + (ends[i].y - starts[i].y) * 1.0f;

            canvas->object_line(canvas->context, start, end);
        }
    }
}
