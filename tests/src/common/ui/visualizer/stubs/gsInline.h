#pragma once

#include <gsKit.h>

void* gsKit_heap_alloc(GSGLOBAL* gs, int qwords, int bytes, int type);
int   gsKit_float_to_int_x(GSGLOBAL* gs, float x);
int   gsKit_float_to_int_y(GSGLOBAL* gs, float y);

static inline int test_uv(float value, unsigned size)
{
    int v = (int)(value * 16);

    if (v < 0)
    {
        v = 0;
    }

    if (v > (int)size * 16)
    {
        v = size * 16;
    }

    if (v >= 16384)
    {
        v = 16383;
    }

    return v;
}

static inline int gsKit_float_to_int_u(const GSTEXTURE* t, float u)
{
    return test_uv(u, t->Width);
}

static inline int gsKit_float_to_int_v(const GSTEXTURE* t, float v)
{
    return test_uv(v, t->Height);
}
