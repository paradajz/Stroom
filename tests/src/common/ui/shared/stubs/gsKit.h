#pragma once

#include <stdint.h>

#define GS_PSM_CT32                                   0
#define GS_FILTER_LINEAR                              1
#define GS_FILTER_NEAREST                             0
#define GS_CMODE_REGION_CLAMP                         2
#define GS_FRAME_1                                    0x4c
#define GS_TEXFLUSH                                   0x3f
#define GS_PRIM_PRIM_TRIANGLE                         3
#define GS_SETREG_UV(u, v)                            ((u64)(u) | ((u64)(v) << 16))
#define GS_PRIM_PRIM_LINE                             1
#define GS_PRIM_PRIM_SPRITE                           6
#define GIF_AD                                        0xe
#define GIF_TAG_AD(n)                                 ((u64)(n) | ((u64)1 << 60))
#define GS_SETREG_FRAME(fbp, fbw, psm, mask)          ((u64)(fbp) | ((u64)(fbw) << 16) | ((u64)(psm) << 24) | ((u64)(mask) << 32))
#define GS_SETREG_PRIM(p, i, t, f, a, aa, fs, c, fix) ((u64)(p) | ((u64)(i) << 3) | ((u64)(t) << 4) | ((u64)(f) << 5) | ((u64)(a) << 6) | ((u64)(aa) << 7) | ((u64)(fs) << 8) | ((u64)(c) << 9) | ((u64)(fix) << 10))
#define GS_SETREG_XYZ2(x, y, z)                       ((u64)(x) | ((u64)(y) << 16) | ((u64)(z) << 32))
#define GSKIT_ALLOC_SYSBUFFER                         0
#define GSKIT_ALLOC_ERROR                             UINT32_MAX
#define GS_SETTING_ON                                 1
#define GS_SETREG_ALPHA(a, b, c, d, e)                ((u64)(a) | ((u64)(b) << 2) | ((u64)(c) << 4) | ((u64)(d) << 6) | ((u64)(e) << 32))
#define GS_SETTING_OFF                                0
#define GS_SETREG_RGBAQ(r, g, b, a, q)                ((u64)(r) | ((u64)(g) << 8) | ((u64)(b) << 16) | ((u64)(a) << 24) | ((u64)(q) << 32))

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;

typedef struct
{
    u32  Width, Height, PSM, Filter, TBW, Vram;
    u32* Mem;
} GSTEXTURE;

typedef struct
{
    int MINU, MAXU, MINV, MAXV;
} GsClamp;

typedef struct
{
    void*    pool_cur;
    unsigned tag_size;
} GSQUEUE;

typedef struct
{
    GSQUEUE* CurQueue;
    int      PrimAlphaEnable;
    u64      PrimAlpha;
    int      Width, Height, PSM, ActiveBuffer, PrimContext, PrimFogEnable, PrimAAEnable, OffsetX, OffsetY;
    u32      ScreenBuffer[2];
    GsClamp* Clamp;
} GSGLOBAL;

u32  gsKit_texture_size(int width, int height, int psm);
u32  gsKit_vram_alloc(GSGLOBAL* gs, u32 size, u8 type);
void gsKit_texture_upload(GSGLOBAL* gs, GSTEXTURE* texture);
void gsKit_prim_sprite_texture(GSGLOBAL* gs, const GSTEXTURE* texture, float x, float y, float u, float v, float right, float bottom, float uu, float vv, int z, u64 color);
void gsKit_set_primalpha(GSGLOBAL* gs, u64 mode, u8 per_pixel);
void gsKit_prim_sprite(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color);
void gsKit_prim_line(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color);
void gsKit_clear(GSGLOBAL* gs, u64 color);
void gsKit_queue_exec(GSGLOBAL* gs);
void gsKit_finish(void);
void gsKit_set_clamp(GSGLOBAL* gs, int mode);
void gsKit_prim_triangle_gouraud(GSGLOBAL* gs, float x0, float y0, float x1, float y1, float x2, float y2, int z, u64 c0, u64 c1, u64 c2);
void gsKit_prim_triangle_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 color);
void gsKit_prim_triangle_goraud_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 c0, u64 c1, u64 c2);
