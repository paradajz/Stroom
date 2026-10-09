#include "util/diagnostics.h"
#include "profiling/benchmark.h"
#include "ui/visualizer/scene.h"
#include "ui/visualizer/color_curve.h"
#include "ui/shared/style.h"
#include "platform/graphics/constants.h"
#include "milkdrop/feedback.h"
#include "ui/visualizer/scene_batch.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <gsInline.h>

#pragma GCC diagnostic pop
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#if STROOM_PRESET_BENCHMARK
#include "platform/time/clock.h"
#endif

/* Batch only adjacent primitives with identical state and submission order.
 * An open batch owns its queue tail: flush before other draws or allocations,
 * GS state/target changes, queue switches or submission. */
#define SCENE_BATCH_CAPACITY 128

#if STROOM_PRESET_BENCHMARK && STROOM_BENCHMARK_PROFILE
#define SCENE_TIMING_BEGIN(name)      uint64_t name = platform_ticks()
#define SCENE_TIMING_END(field, name) (timing.field += platform_ticks() - (name))
#else
#define SCENE_TIMING_BEGIN(name)
#define SCENE_TIMING_END(field, name)
#endif

#if STROOM_PRESET_BENCHMARK
static SceneTiming timing;
#endif

static void line(void* ctx, PresetVertex a, PresetVertex b, float opacity);
static void blend(void* ctx, int additive);
static void texture_flush(GSGLOBAL* gs);
static void render_target(GSGLOBAL* gs, u32 address);

static GSTEXTURE object_texture;
static int       object_texture_valid;
static int       history_valid;
static int       raw_history_valid, raw_allocation_failed;
static u32       raw_history_vram;

/**
 * @brief Adjacent GS primitives sharing state and submission order.
 */
static struct
{
    GSGLOBAL* gs;        /**< Borrowed GS context for this batch. */
    u64       prim;      /**< Packed GS PRIM value. */
    unsigned  count;     /**< Number of queued primitives. */
    unsigned  registers; /**< 64-bit register words per primitive. */
    float     last_x;    /**< Last endpoint x in pixels, cached for coordinate conversion. */
    float     last_y;    /**< Last endpoint y in pixels, cached for coordinate conversion. */
    u64       last_xyz;  /**< Packed GS XYZ2 for the last endpoint. */
    uint64_t* vertices;  /**< Vertex words within the reserved queue packet. */
    uint64_t* packet;    /**< Start of the reserved GIF packet. */
    GSQUEUE*  queue;     /**< Queue owning the reservation until flush. */
} primitive_batch;

/**
 * @brief Submit pending primitives and clear the batch.
 */
static void batch_flush(void)
{
    if (!primitive_batch.count)
    {
        return;
    }

    unsigned words    = scene_batch_words(primitive_batch.count, primitive_batch.registers);
    unsigned reserved = scene_batch_words(SCENE_BATCH_CAPACITY, primitive_batch.registers);

    /* Only this reservation may occupy the queue tail. Return its unused words
     * without changing earlier allocations. gsKit's allocator remains
     * responsible for DMA-chain boundaries. */
    primitive_batch.queue->pool_cur = primitive_batch.packet + words;
    primitive_batch.queue->tag_size -= (reserved - words) / 2;

    scene_batch_finish(primitive_batch.packet, primitive_batch.prim, primitive_batch.count, primitive_batch.registers);

    primitive_batch.count = 0;
}

/** @brief Reserve a full batch directly in the current gsKit queue. */
static void batch_reserve(void)
{
    unsigned words = scene_batch_words(SCENE_BATCH_CAPACITY, primitive_batch.registers);

    primitive_batch.queue = primitive_batch.gs->CurQueue;

    /* GIF_AD closes the preceding gsKit tag. Its allocator supplies the first
     * tag, hence the two-word/one-qword subtraction here. */
    primitive_batch.packet   = gsKit_heap_alloc(primitive_batch.gs, words / 2 - 1, (words - 2) * 8, GIF_AD);
    primitive_batch.vertices = primitive_batch.packet + SCENE_BATCH_HEADER_WORDS;
}

/**
 * @brief Append a line or sprite, flushing on capacity or primitive-type changes.
 *
 * @param gs GS drawing context.
 * @param sprite Nonzero for a sprite; zero for a line.
 * @param ax First horizontal pixel coordinate.
 * @param ay First vertical pixel coordinate.
 * @param bx Second horizontal pixel coordinate.
 * @param by Second vertical pixel coordinate.
 * @param ca First packed GS color.
 * @param cb Second color for a line; ignored for sprites.
 */
static void batch_pair(GSGLOBAL* gs, int sprite, float ax, float ay, float bx, float by, u64 ca, u64 cb)
{
    unsigned registers = sprite ? 3 : 4;

    /* All GS state/target changes in this renderer flush first. State is
     * therefore constant within a batch; build PRIM only at batch start. */

    if (primitive_batch.count && (primitive_batch.gs != gs || primitive_batch.registers != registers || primitive_batch.count == SCENE_BATCH_CAPACITY))
    {
        batch_flush();
    }

    if (!primitive_batch.count)
    {
        primitive_batch.gs        = gs;
        primitive_batch.prim      = GS_SETREG_PRIM(sprite ? GS_PRIM_PRIM_SPRITE : GS_PRIM_PRIM_LINE, !sprite, 0, gs->PrimFogEnable, gs->PrimAlphaEnable, gs->PrimAAEnable, 0, gs->PrimContext, 0);
        primitive_batch.registers = registers;

        batch_reserve();
    }

    u64 first_xyz;

    if (!sprite && primitive_batch.count && ax == primitive_batch.last_x && ay == primitive_batch.last_y)
    {
        first_xyz = primitive_batch.last_xyz;
    }
    else
    {
        first_xyz = GS_SETREG_XYZ2(gsKit_float_to_int_x(gs, ax), gsKit_float_to_int_y(gs, ay), 1);
    }

    u64       last_xyz = GS_SETREG_XYZ2(gsKit_float_to_int_x(gs, bx), gsKit_float_to_int_y(gs, by), 1);
    uint64_t* q        = primitive_batch.vertices + primitive_batch.count * registers;

    *q++ = ca;
    *q++ = first_xyz;

    if (!sprite)
    {
        *q++ = cb;
    }

    *q                       = last_xyz;
    primitive_batch.last_x   = bx;
    primitive_batch.last_y   = by;
    primitive_batch.last_xyz = last_xyz;

    ++primitive_batch.count;
}

/**
 * @brief Borrowed texture and presentation state for feedback triangle callbacks.
 */
typedef struct
{
    GSGLOBAL*            gs;        /**< GS drawing context. */
    GSTEXTURE*           texture;   /**< Feedback texture to sample. */
    u64                  tint;      /**< Packed uniform GS tint when corner shading is disabled. */
    const MilkComposite* composite; /**< Optional borrowed corner-shading settings. */
    int                  started;   /**< Nonzero after gsKit established this pass's texture state. */
    int                  weight;    /**< GS color multiplier for shaded drawing; 128 is unity. */
} TextureDraw;

/**
 * @brief Draw one feedback triangle with optional presentation shading.
 *
 * @param ctx TextureDraw context.
 * @param v Three pixel-space vertices with normalized texture coordinates.
 */
static void texture_triangle(void* ctx, const FeedbackVertex* v)
{
    SCENE_TIMING_BEGIN(command_begin);

    TextureDraw* d         = ctx;
    u64          colors[3] = { d->tint, d->tint, d->tint };

    if (d->composite)
    {
        for (unsigned i = 0; i < 3; ++i)
        {
            float rgb[3];

            milk_shade_at(d->composite, v[i].x / DISPLAY_WIDTH, v[i].y / DISPLAY_HEIGHT, rgb);

            colors[i] = GS_SETREG_RGBAQ((int)(d->weight * rgb[0] + .5f), (int)(d->weight * rgb[1] + .5f), (int)(d->weight * rgb[2] + .5f), 128, 0);
        }
    }

    /* Let gsKit establish filter, texture and primitive state once per pass.
     * Later triangles retain that state and order; flat/Gouraud shading selects
     * distinct register layouts. Flush each feedback/echo pass before changing
     * texture, blend state or render target. */

    if (!d->started)
    {
        batch_flush();

        d->started = 1;

        if (d->composite)
        {
            gsKit_prim_triangle_goraud_texture(d->gs, d->texture, v[0].x, v[0].y, .5f + v[0].u * (d->texture->Width - 1), .5f + v[0].v * (d->texture->Height - 1), v[1].x, v[1].y, .5f + v[1].u * (d->texture->Width - 1), .5f + v[1].v * (d->texture->Height - 1), v[2].x, v[2].y, .5f + v[2].u * (d->texture->Width - 1), .5f + v[2].v * (d->texture->Height - 1), 1, colors[0], colors[1], colors[2]);
        }
        else
        {
            gsKit_prim_triangle_texture(d->gs, d->texture, v[0].x, v[0].y, .5f + v[0].u * (d->texture->Width - 1), .5f + v[0].v * (d->texture->Height - 1), v[1].x, v[1].y, .5f + v[1].u * (d->texture->Width - 1), .5f + v[1].v * (d->texture->Height - 1), v[2].x, v[2].y, .5f + v[2].u * (d->texture->Width - 1), .5f + v[2].v * (d->texture->Height - 1), 1, d->tint);
        }

        SCENE_TIMING_END(command_ticks, command_begin);
        return;
    }

    if (!primitive_batch.count)
    {
        primitive_batch.gs        = d->gs;
        primitive_batch.prim      = GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE, d->composite != NULL, 1, d->gs->PrimFogEnable, d->gs->PrimAlphaEnable, d->gs->PrimAAEnable, 1, d->gs->PrimContext, 0);
        primitive_batch.registers = d->composite ? SCENE_SHADED_TEXTURED_TRIANGLE_REGISTERS : SCENE_TEXTURED_TRIANGLE_REGISTERS;

        batch_reserve();
    }

    uint64_t* q = primitive_batch.vertices + primitive_batch.count * primitive_batch.registers;

    for (unsigned i = 0; i < 3; ++i)
    {
        if (d->composite || i == 0)
        {
            *q++ = colors[i];
        }

        int u = gsKit_float_to_int_u(d->texture, .5f + v[i].u * (d->texture->Width - 1));
        int t = gsKit_float_to_int_v(d->texture, .5f + v[i].v * (d->texture->Height - 1));

        *q++ = GS_SETREG_UV(u, t);
        *q++ = GS_SETREG_XYZ2(gsKit_float_to_int_x(d->gs, v[i].x), gsKit_float_to_int_y(d->gs, v[i].y), 1);
    }

    if (++primitive_batch.count == SCENE_BATCH_CAPACITY)
    {
        batch_flush();
    }

    SCENE_TIMING_END(command_ticks, command_begin);
}

/**
 * @brief Allocate the clean feedback-history buffer once.
 *
 * @param gs GS drawing context.
 * @return 1 if storage is available; 0 if VRAM allocation failed.
 */
static int ensure_raw_history(GSGLOBAL* gs)
{
    if (!raw_history_vram && !raw_allocation_failed)
    {
        /* The two framebuffers and clean CT32 history fit in GS VRAM.
         * The optional 64 KiB cover texture also fits; no depth buffer is used. */
        raw_history_vram = gsKit_vram_alloc(gs, gs->Width * gs->Height * 4, GSKIT_ALLOC_SYSBUFFER);

        if (raw_history_vram == GSKIT_ALLOC_ERROR)
        {
            raw_allocation_failed = 1;
            raw_history_vram      = 0;

            STROOM_LOG("insufficient GS VRAM for feedback/presentation effects");
        }
    }

    if (!raw_history_vram)
    {
        return 0;
    }

    return 1;
}

void scene_close(void)
{
    scene_color_curve_close();
    memset(&primitive_batch, 0, sizeof(primitive_batch));
    memset(&object_texture, 0, sizeof(object_texture));

    object_texture_valid = history_valid = raw_history_valid = raw_allocation_failed = 0;
    raw_history_vram                                                                 = 0;
}

void scene_reset(void)
{
    history_valid     = 0;
    raw_history_valid = 0;
}

/**
 * @brief Draw warped previous MilkDrop image and its motion vectors.
 *
 * @param gs GS drawing context.
 * @param d Director whose vertex state may advance.
 */
static void feedback(GSGLOBAL* gs, Director* d)
{
    /* Borrow previous display or preserved raw MilkDrop image as a texture. Motion
     * vectors reuse the raw scratch image so the displayed buffer is untouched.
     * queue_exec waits for the previous GS FINISH. */
    GSTEXTURE texture = { 0 };

    texture.Width  = gs->Width;
    texture.Height = gs->Height;
    texture.PSM    = gs->PSM;
    texture.TBW    = gs->Width / PS2_GS_BUFFER_WIDTH_PIXELS;
    texture.Vram   = raw_history_valid ? raw_history_vram : gs->ScreenBuffer[(gs->ActiveBuffer & 1) ^ 1];
    texture.Filter = GS_FILTER_LINEAR;

    u64* q = gsKit_heap_alloc(gs, 1, 16, GIF_AD);

    *q++            = GIF_TAG_AD(1);
    *q++            = GIF_AD;
    *q++            = 0;
    *q++            = GS_TEXFLUSH;
    gs->Clamp->MINU = 0;
    gs->Clamp->MAXU = gs->Width - 1;
    gs->Clamp->MINV = 0;
    gs->Clamp->MAXV = gs->Height - 1;

    gsKit_set_clamp(gs, GS_CMODE_REGION_CLAMP);

    static FeedbackMesh mesh;

    SCENE_TIMING_BEGIN(mesh_begin);
    feedback_build(&d->current, d->transitioning ? &d->next : NULL, director_progress(d), &mesh);
    SCENE_TIMING_END(mesh_ticks, mesh_begin);

    MilkMotion motion;

    if (milk_motion_parameters(&d->current, d->transitioning ? &d->next : NULL, director_mix(d), &motion) && ensure_raw_history(gs))
    {
        /* The reference writes vectors into previous raw history BEFORE warp. */

        if (!raw_history_valid)
        {
            /* Never write vectors into the buffer currently being displayed. */
            texture_flush(gs);
            render_target(gs, raw_history_vram);

            texture.Filter = GS_FILTER_NEAREST;

            gsKit_prim_sprite_texture(gs, &texture, 0, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT, 1, ui_color(UI_COLOR_TEXTURE_NEUTRAL));

            texture.Filter = GS_FILTER_LINEAR;
            texture.Vram   = raw_history_vram;
        }

        render_target(gs, raw_history_vram);

        PresetCanvas canvas = { gs, NULL, line, NULL, blend, 1, NULL, NULL, NULL, NULL };

        milk_motion_draw(&motion, &mesh, &canvas);
        render_target(gs, gs->ScreenBuffer[gs->ActiveBuffer & 1]);
        texture_flush(gs);

        gs->PrimAlphaEnable = GS_SETTING_OFF;
    }

    int decay = (int)(128 * mesh.decay + .5f);

    if (decay > 128)
    {
        decay = 128;
    }

    u64 tint = GS_SETREG_RGBAQ(decay, decay, decay, 128, 0);

    object_texture       = texture;
    object_texture_valid = 1;

    TextureDraw draw = { gs, &texture, tint, NULL, 0, 0 };

    SCENE_TIMING_BEGIN(emit_begin);
    feedback_emit(&mesh, texture_triangle, &draw);
    SCENE_TIMING_BEGIN(flush_begin);
    batch_flush();
    SCENE_TIMING_END(command_ticks, flush_begin);
    SCENE_TIMING_END(emit_ticks, emit_begin);
}

/**
 * @brief Flush pending primitives and invalidate the GS texture cache.
 *
 * @param gs GS drawing context.
 */
static void texture_flush(GSGLOBAL* gs)
{
    batch_flush();

    u64* q = gsKit_heap_alloc(gs, 1, 16, GIF_AD);

    *q++ = GIF_TAG_AD(1);
    *q++ = GIF_AD;
    *q++ = 0;
    *q++ = GS_TEXFLUSH;
}

/**
 * @brief Flush pending primitives and select a GS render buffer.
 *
 * @param gs GS drawing context.
 * @param address VRAM buffer address in bytes.
 */
static void render_target(GSGLOBAL* gs, u32 address)
{
    batch_flush();

    u64* q = gsKit_heap_alloc(gs, 1, 16, GIF_AD);

    *q++ = GIF_TAG_AD(1);
    *q++ = GIF_AD;
    *q++ = GS_SETREG_FRAME(address / PS2_GS_FRAME_PAGE_BYTES, gs->Width / PS2_GS_BUFFER_WIDTH_PIXELS, gs->PSM, 0);
    *q++ = GS_FRAME_1 + gs->PrimContext;
}

/**
 * @brief Apply presentation effects while optionally preserving clean MilkDrop image.
 *
 * @param gs GS drawing context.
 * @param director Preset transition state.
 * @param preserve_overlays Nonzero to preserve MilkDrop image before overlays.
 */
static void composite(GSGLOBAL* gs, const Director* director, int preserve_overlays)
{
    batch_flush();

    MilkComposite composite;

    milk_composite(&director->current, director->transitioning ? &director->next : NULL, director_mix(director), &composite);

    raw_history_valid = 0;

    int identity = composite.base >= 1 && !composite.shaded && composite.gamma == 1 && !composite.invert && !composite.darken && !composite.brighten && !composite.solarize;

    if (identity && !preserve_overlays)
    {
        return;
    }

    if (!ensure_raw_history(gs))
    {
        return;
    }

    GSTEXTURE texture = { 0 };

    texture.Width       = gs->Width;
    texture.Height      = gs->Height;
    texture.PSM         = gs->PSM;
    texture.TBW         = gs->Width / PS2_GS_BUFFER_WIDTH_PIXELS;
    texture.Vram        = gs->ScreenBuffer[gs->ActiveBuffer & 1];
    texture.Filter      = GS_FILTER_NEAREST;
    gs->PrimAlphaEnable = GS_SETTING_OFF;
    gs->Clamp->MINU     = 0;
    gs->Clamp->MAXU     = gs->Width - 1;
    gs->Clamp->MINV     = 0;
    gs->Clamp->MAXV     = gs->Height - 1;

    gsKit_set_clamp(gs, GS_CMODE_REGION_CLAMP);

    /* Save raw MilkDrop image before presentation. The next feedback step samples
     * this copy, never the displayed echo. All draws stay in the GIF queue. */
    texture_flush(gs);
    render_target(gs, raw_history_vram);
    gsKit_prim_sprite_texture(gs, &texture, 0, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT, 1, ui_color(UI_COLOR_TEXTURE_NEUTRAL));
    render_target(gs, gs->ScreenBuffer[gs->ActiveBuffer & 1]);
    texture_flush(gs);

    texture.Vram      = raw_history_vram;
    raw_history_valid = 1;

    /* Keep overlay text out of feedback, without changing the MilkDrop image. */

    if (identity)
    {
        return;
    }

    /* Classic fGammaAdj is additive gain, not a power-law gamma curve.
     * Bound it to eight passes; reuse the same raw image for every layer. */
    unsigned passes = (unsigned)ceilf(composite.gamma);

    if (!passes)
    {
        passes = 1;
    }

    for (unsigned pass = 0; pass < passes; ++pass)
    {
        float gain       = fminf(1, fmaxf(0, composite.gamma - pass));
        int   weights[2] = { (int)(128 * composite.echo[0].alpha + .5f), (int)(128 * composite.echo[1].alpha + .5f) };

        if (weights[1] > 128 - weights[0])
        {
            weights[1] = 128 - weights[0];
        }

        int base = (int)((128 - weights[0] - weights[1]) * gain + .5f);

        texture.Filter      = GS_FILTER_NEAREST;
        gs->PrimAlphaEnable = pass ? GS_SETTING_ON : GS_SETTING_OFF;

        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);

        if (composite.shaded)
        {
            MilkEcho    identity = { 1, 1, 0, 0 };
            TextureDraw draw     = { gs, &texture, 0, &composite, 0, base };

            SCENE_TIMING_BEGIN(emit_begin);
            feedback_echo_emit(&identity, texture_triangle, &draw);
            SCENE_TIMING_BEGIN(flush_begin);
            batch_flush();
            SCENE_TIMING_END(command_ticks, flush_begin);
            SCENE_TIMING_END(emit_ticks, emit_begin);
        }
        else
        {
            gsKit_prim_sprite_texture(gs, &texture, 0, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT, 1, GS_SETREG_RGBAQ(base, base, base, 128, 0));
        }

        texture.Filter      = GS_FILTER_LINEAR;
        gs->PrimAlphaEnable = GS_SETTING_ON;

        for (unsigned i = 0; i < 2; ++i)
        {
            int weight = (int)(weights[i] * gain + .5f);

            if (!weight)
            {
                continue;
            }

            TextureDraw draw = { gs, &texture, GS_SETREG_RGBAQ(weight, weight, weight, 128, 0), composite.shaded ? &composite : NULL, 0, weight };

            SCENE_TIMING_BEGIN(emit_begin);
            feedback_echo_emit(&composite.echo[i], texture_triangle, &draw);
            SCENE_TIMING_BEGIN(flush_begin);
            batch_flush();
            SCENE_TIMING_END(command_ticks, flush_begin);
            SCENE_TIMING_END(emit_ticks, emit_begin);
        }
    }

    if (composite.darken || composite.brighten || composite.solarize)
    {
        scene_color_curve(gs, composite.brighten, composite.darken, composite.solarize);
    }

    if (composite.invert)
    {
        /* (0 - destination)*1 + white, using FIX = 128. */
        gs->PrimAlphaEnable = GS_SETTING_ON;

        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(2, 1, 2, 0, 128), 0);
        gsKit_prim_sprite(gs, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 1, GS_SETREG_RGBAQ(255, 255, 255, 128, 0));
    }
}

/**
 * @brief Pack clamped vertex color and opacity, caching repeated conversions.
 *
 * @param p Vertex containing RGB channels on a 0..255 scale.
 * @param alpha Opacity in 0..1.
 * @return Packed GS RGBAQ value.
 */
static u64 color(PresetVertex p, float alpha)
{
    /* Uniform-color waves reuse this value for thousands of endpoints.
     * This cache contains only the pure color conversion, never GS state. */
    static float r, g, b, a;
    static u64   packed;
    static int   valid;

    if (valid && p.r == r && p.g == g && p.b == b && alpha == a)
    {
        return packed;
    }

    r      = p.r;
    g      = p.g;
    b      = p.b;
    a      = alpha;
    packed = GS_SETREG_RGBAQ((int)fminf(255, fmaxf(0, p.r)), (int)fminf(255, fmaxf(0, p.g)), (int)fminf(255, fmaxf(0, p.b)), (int)(128 * fminf(1, fmaxf(0, alpha))), 0);
    valid  = 1;

    return packed;
}

/**
 * @brief Submit a Gouraud-shaded triangle.
 *
 * @param ctx GS drawing context.
 * @param a First vertex.
 * @param b Second vertex.
 * @param c Third vertex.
 * @param opacity Opacity in 0..1.
 */
static void triangle(void* ctx, PresetVertex a, PresetVertex b, PresetVertex c, float opacity)
{
    PROFILE_BEGIN(draw_begin);
    batch_flush();
    gsKit_prim_triangle_gouraud(ctx, a.x, a.y, b.x, b.y, c.x, c.y, 1, color(a, opacity), color(b, opacity), color(c, opacity));
    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Batch a colored line.
 *
 * @param ctx GS drawing context.
 * @param a Start vertex.
 * @param b End vertex.
 * @param opacity Opacity in 0..1.
 */
static void line(void* ctx, PresetVertex a, PresetVertex b, float opacity)
{
    PROFILE_BEGIN(draw_begin);
    batch_pair(ctx, 0, a.x, a.y, b.x, b.y, color(a, opacity), color(b, opacity));
    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Batch a square sprite centered on a vertex.
 *
 * @param ctx GS drawing context.
 * @param p Center position and color.
 * @param size Half-size in pixels.
 * @param opacity Opacity in 0..1.
 */
static void sprite(void* ctx, PresetVertex p, float size, float opacity)
{
    PROFILE_BEGIN(draw_begin);
    batch_pair(ctx, 1, p.x - size, p.y - size, p.x + size, p.y + size, color(p, opacity), 0);
    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Flush geometry and select the GS blending equation.
 *
 * @param ctx GS drawing context.
 * @param additive Nonzero for additive blending; zero for alpha blending.
 */
static void blend(void* ctx, int additive)
{
    PROFILE_BEGIN(draw_begin);
    batch_flush();

    GSGLOBAL* gs = ctx;

    gs->PrimAlphaEnable = GS_SETTING_ON;

    gsKit_set_primalpha(gs, additive ? GS_SETREG_ALPHA(0, 2, 0, 1, 0) : GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Draw the center-darkening diamond.
 *
 * @param ctx GS drawing context.
 * @param opacity Opacity multiplier.
 */
static void darken_center(void* ctx, float opacity)
{
    PROFILE_BEGIN(draw_begin);
    batch_flush();

    GSGLOBAL*   gs       = ctx;
    float       r        = MILK_CENTER_RADIUS;
    const float center_x = gs->Width / 2.0f, center_y = gs->Height / 2.0f;
    const float x[4]   = { center_x - r, center_x, center_x + r, center_x };
    const float y[4]   = { center_y, center_y + r, center_y, center_y - r };
    u64         center = GS_SETREG_RGBAQ(0, 0, 0, (int)(128 * MILK_CENTER_ALPHA * opacity + .5f), 0);
    u64         edge   = GS_SETREG_RGBAQ(0, 0, 0, 0, 0);

    for (unsigned i = 0; i < 4; ++i)
    {
        gsKit_prim_triangle_gouraud(gs, center_x, center_y, x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4], 1, center, edge, edge);
    }

    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Pack custom-object color with GS texture modulation scaling.
 *
 * @param v Vertex containing RGB and opacity.
 * @param textured Nonzero for texture modulation.
 * @return Packed GS RGBAQ value.
 */
static u64 object_color(MilkVertex v, int textured)
{
    float scale = textured ? 128.0f / UINT8_MAX : 1;

    return GS_SETREG_RGBAQ((int)(v.r * scale), (int)(v.g * scale), (int)(v.b * scale), (int)(128 * v.a), 0);
}

/**
 * @brief Submit a custom triangle using the current feedback texture when requested.
 *
 * @param ctx GS drawing context.
 * @param v Three custom vertices.
 * @param textured Nonzero for textured drawing.
 */
static void object_triangle(void* ctx, const MilkVertex* v, int textured)
{
    PROFILE_BEGIN(draw_begin);
    batch_flush();

    GSGLOBAL* gs = ctx;

    if (textured)
    {
        if (!object_texture_valid)
        {
            PROFILE_END(draw_commands, draw_begin);
            return;
        }

        gsKit_prim_triangle_goraud_texture(gs, &object_texture, v[0].x, v[0].y, .5f + (object_texture.Width - 1) * v[0].u, .5f + (object_texture.Height - 1) * v[0].v, v[1].x, v[1].y, .5f + (object_texture.Width - 1) * v[1].u, .5f + (object_texture.Height - 1) * v[1].v, v[2].x, v[2].y, .5f + (object_texture.Width - 1) * v[2].u, .5f + (object_texture.Height - 1) * v[2].v, 1, object_color(v[0], 1), object_color(v[1], 1), object_color(v[2], 1));
    }
    else
    {
        gsKit_prim_triangle_gouraud(gs, v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y, 1, object_color(v[0], 0), object_color(v[1], 0), object_color(v[2], 0));
    }

    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Batch a custom-object line using per-vertex opacity.
 *
 * @param ctx GS drawing context.
 * @param a Start vertex.
 * @param b End vertex.
 */
static void object_line(void* ctx, MilkVertex a, MilkVertex b)
{
    PROFILE_BEGIN(draw_begin);
    batch_pair(ctx, 0, a.x, a.y, b.x, b.y, object_color(a, 0), object_color(b, 0));
    PROFILE_END(draw_commands, draw_begin);
}

/**
 * @brief Batch fully visible wave segments and thickness copies without intermediate callbacks.
 *
 * Preserve scalar-path interpolation rounding and batch boundaries.
 *
 * @param ctx GS drawing context.
 * @param a Start vertex.
 * @param mid Smoothed midpoint.
 * @param b End vertex.
 * @param opacity Canvas opacity multiplier.
 * @param copies One for thin waves; four for thick waves.
 */
static void object_wave_segments(void* ctx, const MilkVertex* a, const MilkVertex* mid, const MilkVertex* b, float opacity, unsigned copies)
{
    PROFILE_BEGIN(draw_begin);

    GSGLOBAL* gs = ctx;

    if (primitive_batch.count && (primitive_batch.gs != gs || primitive_batch.registers != 4 || primitive_batch.count == SCENE_BATCH_CAPACITY))
    {
        batch_flush();
    }

    if (!primitive_batch.count)
    {
        primitive_batch.gs        = gs;
        primitive_batch.prim      = GS_SETREG_PRIM(GS_PRIM_PRIM_LINE, 1, 0, gs->PrimFogEnable, gs->PrimAlphaEnable, gs->PrimAAEnable, 0, gs->PrimContext, 0);
        primitive_batch.registers = 4;

        batch_reserve();
    }

    const MilkVertex* v[3] = { a, mid, b };
    u64               colors[4];

    for (unsigned i = 0; i < 4; ++i)
    {
        const MilkVertex* lo           = v[i / 2];
        const MilkVertex* hi           = v[i / 2 + 1];
        float             t            = i & 1;
        MilkVertex        color_vertex = { 0 };

        color_vertex.r = lo->r + (hi->r - lo->r) * t;
        color_vertex.g = lo->g + (hi->g - lo->g) * t;
        color_vertex.b = lo->b + (hi->b - lo->b) * t;
        color_vertex.a = (lo->a + (hi->a - lo->a) * t) * opacity;
        colors[i]      = object_color(color_vertex, 0);
    }

    /* Colors are identical for all thickness copies. */

    for (unsigned copy = 0; copy < copies; ++copy)
    {
        /* Most groups fit entirely. Overflow uses the existing append routine
         * so even GIF packet splitting is identical to ordinary line drawing. */
        int       contiguous = primitive_batch.count <= SCENE_BATCH_CAPACITY - 2;
        uint64_t* q          = primitive_batch.vertices + primitive_batch.count * 4;

        /* Keep offset-before-interpolation rounding identical to the scalar path. */
        const float dx = copy == 1 || copy == 2, dy = copy >= 2;
        float       x[3] = { a->x + dx, mid->x + dx, b->x + dx };
        float       y[3] = { a->y + dy, mid->y + dy, b->y + dy };

        for (unsigned i = 0; i < 2; ++i)
        {
            float ax = x[i] + (x[i + 1] - x[i]) * 0.0f;
            float ay = y[i] + (y[i + 1] - y[i]) * 0.0f;
            float bx = x[i] + (x[i + 1] - x[i]) * 1.0f;
            float by = y[i] + (y[i + 1] - y[i]) * 1.0f;

            if (!contiguous)
            {
                batch_pair(gs, 0, ax, ay, bx, by, colors[i * 2], colors[i * 2 + 1]);
                continue;
            }

            u64 first_xyz;

            if (primitive_batch.count && ax == primitive_batch.last_x && ay == primitive_batch.last_y)
            {
                first_xyz = primitive_batch.last_xyz;
            }
            else
            {
                first_xyz = GS_SETREG_XYZ2(gsKit_float_to_int_x(gs, ax), gsKit_float_to_int_y(gs, ay), 1);
            }

            u64 last_xyz = GS_SETREG_XYZ2(gsKit_float_to_int_x(gs, bx), gsKit_float_to_int_y(gs, by), 1);

            *q++                     = colors[i * 2];
            *q++                     = first_xyz;
            *q++                     = colors[i * 2 + 1];
            *q++                     = last_xyz;
            primitive_batch.last_x   = bx;
            primitive_batch.last_y   = by;
            primitive_batch.last_xyz = last_xyz;

            ++primitive_batch.count;
        }
    }

    PROFILE_END(draw_commands, draw_begin);
}

void scene_draw(GSGLOBAL* gs, Director* director, const MusicFeatures* s, int use_feedback, int preserve_overlays)
{
#if STROOM_PRESET_BENCHMARK
    timing = (SceneTiming){ 0 };
#endif
    batch_flush();

    object_texture_valid = 0;

    float mix = director_mix(director);

    gs->PrimAlphaEnable = GS_SETTING_OFF;

    if (history_valid && use_feedback)
    {
        feedback(gs, director);
    }
    else
    {
        gsKit_clear(gs, GS_SETREG_RGBAQ(2, 3, 9, 128, 0));
    }

    PresetCanvas canvas = { gs, triangle, line, sprite, blend, 1 - mix, darken_center, object_triangle, object_line, NULL };

    canvas.object_wave_segments = object_wave_segments;

    if (director->transitioning)
    {
        canvas.opacity = 1;

        milk_draw_transition(&director->current, &director->next, mix, &canvas, s);
    }
    else
    {
        preset_draw(&director->current, &canvas, s);
    }

    /* Submit pending waveform commands before composite. */
    PROFILE_BEGIN(flush_begin);
    batch_flush();
    PROFILE_END(draw_commands, flush_begin);
#if STROOM_PRESET_BENCHMARK
    uint64_t emitted_before = timing.emit_ticks;
#endif
    PROFILE_BEGIN(presentation_begin);
    composite(gs, director, preserve_overlays);
    PROFILE_END(presentation, presentation_begin);
#if STROOM_PRESET_BENCHMARK
    benchmark_profile.presentation -= timing.emit_ticks - emitted_before;
#endif
    gs->PrimAlphaEnable = GS_SETTING_OFF;

    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    history_valid = 1;
}

#if STROOM_PRESET_BENCHMARK
SceneTiming scene_timing(void)
{
    return timing;
}
#endif
