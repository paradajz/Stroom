#include "ui/frame/frame.h"
#include "application.h"
#include "platform/graphics/display.h"
#include "ui/shared/style.h"
#include "ui/shared/text.h"
#include "ui/shared/track_layout.h"
#include "ui/shared/level_meter.h"
#include "ui/artwork/view.h"
#include "ui/visualizer/scene.h"
#include "ui/visualizer/overlays.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_ITEMS      512
#define CAPTURE_LOGO_ITEMS 1536

static struct
{
    float left, top, right, bottom;
    u64   color;
} shapes[CAPTURE_ITEMS];

static struct
{
    float left, top, scale;
    int   clipped;
    u64   color;
    char  text[128];
} labels[CAPTURE_ITEMS];

static struct
{
    float    x[3], y[3];
    unsigned vertices;
    u64      color;
} logo_shapes[CAPTURE_LOGO_ITEMS];

static unsigned logo_shape_count, logo_line_count;

static unsigned          shape_count, label_count, scene_count, cover_count;
static float             captured_cover_top, captured_cover_side;
static int               scene_overlays;
static int               artwork_available;
static PlayerState       player;
static AppSettings       settings;
static MilkdropRuntime   visualizer;
static Audio             audio;
static AudioSourceStatus source;
static GSGLOBAL          drawing;
static unsigned          presentation_step;
static uint32_t          presentation_started;
static unsigned          presentation_interval;
static int               display_close_ok, display_open_failure;
static unsigned          display_opens, display_closes;
static GSGLOBAL*         closed_context;
static unsigned          texture_resets, scene_closes;

#if STROOM_PRESET_BENCHMARK
static unsigned timer_reads;
static uint64_t timer_base;

uint64_t platform_ticks(void)
{
    static const unsigned steps[] = { 0, 2, 4 };
    static const uint64_t ticks[] = { 11, 29, 53 };

    TEST_ASSERT_LESS_THAN_UINT(3, timer_reads);
    TEST_ASSERT_EQUAL_UINT(steps[timer_reads], presentation_step);

    return timer_base + ticks[timer_reads++];
}
#endif

void gsKit_queue_exec(GSGLOBAL* gs)
{
    TEST_ASSERT_TRUE(gs == &drawing);
    TEST_ASSERT_GREATER_THAN_UINT(0, scene_count);
    TEST_ASSERT_EQUAL_INT(GS_SETTING_OFF, gs->PrimAlphaEnable);
    TEST_ASSERT_EQUAL_UINT(0, presentation_step++);
}

void gsKit_finish(void)
{
    TEST_ASSERT_EQUAL_UINT(1, presentation_step++);
}

void scene_close(void)
{
    TEST_ASSERT_TRUE(display_close_ok);
    ++scene_closes;
}

void ui_artwork_reset_texture(void)
{
    TEST_ASSERT_TRUE(display_close_ok);
    ++texture_resets;
}

void ui_artwork_yield(uint32_t frame_started)
{
    TEST_ASSERT_EQUAL_UINT(2, presentation_step++);
    TEST_ASSERT_EQUAL_UINT32(presentation_started, frame_started);
}

void platform_display_present(GSGLOBAL* gs, unsigned refresh_interval)
{
    TEST_ASSERT_TRUE(gs == &drawing);
    TEST_ASSERT_EQUAL_UINT(3, presentation_step++);
    TEST_ASSERT_EQUAL_UINT(presentation_interval, refresh_interval);
}

void gsKit_set_primalpha(GSGLOBAL* gs, u64 mode, u8 per_pixel)
{
    TEST_ASSERT_EQUAL_UINT(0, per_pixel);

    gs->PrimAlpha = mode;
}

void scene_draw(GSGLOBAL* gs, Director* director, const MusicFeatures* s, int use_feedback, int preserve_overlays)
{
    (void)director;
    (void)s;
    (void)use_feedback;
    ++scene_count;

    scene_overlays      = preserve_overlays;
    gs->PrimAlphaEnable = GS_SETTING_OFF;
}

void ui_level_update(const float rms[2], int active, uint32_t now_ms)
{
    (void)rms;
    (void)active;
    (void)now_ms;
}

void ui_level_values(unsigned channel, float* rms)
{
    (void)channel;

    *rms = 0.5f;
}

int ui_artwork_draw_at(GSGLOBAL* gs, float cover_left, float cover_top, float cover_side, unsigned opacity)
{
    (void)cover_left;
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs->PrimAlphaEnable);
    TEST_ASSERT_EQUAL_UINT64(GS_SETREG_ALPHA(0, 1, 0, 1, 0), gs->PrimAlpha);
    TEST_ASSERT_EQUAL_UINT(115, opacity);

    if (!artwork_available)
    {
        return 0;
    }

    ++cover_count;

    captured_cover_top  = cover_top;
    captured_cover_side = cover_side;

    return 1;
}

void gsKit_prim_sprite(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color)
{
    (void)z;
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs->PrimAlphaEnable);
    TEST_ASSERT_EQUAL_UINT(115, (color >> 24) & 255);
    TEST_ASSERT_TRUE(isfinite(x) && isfinite(y) && isfinite(right) && isfinite(bottom));
    TEST_ASSERT_TRUE(right >= x && bottom >= y);
    TEST_ASSERT_LESS_THAN_UINT(CAPTURE_ITEMS, shape_count);

    shapes[shape_count].left    = x;
    shapes[shape_count].top     = y;
    shapes[shape_count].right   = right;
    shapes[shape_count].bottom  = bottom;
    shapes[shape_count++].color = color;
}

static void capture_logo(GSGLOBAL* gs, const float* x, const float* y, unsigned vertices, u64 color)
{
    TEST_ASSERT_EQUAL_PTR(&drawing, gs);
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs->PrimAlphaEnable);
    TEST_ASSERT_EQUAL_UINT(UI_OVERLAY_ALPHA, (color >> 24) & 255);
    TEST_ASSERT_LESS_THAN_UINT(CAPTURE_LOGO_ITEMS, logo_shape_count);

    for (unsigned i = 0; i < vertices; ++i)
    {
        TEST_ASSERT_TRUE(isfinite(x[i]) && isfinite(y[i]));

        logo_shapes[logo_shape_count].x[i] = x[i];
        logo_shapes[logo_shape_count].y[i] = y[i];
    }

    logo_shapes[logo_shape_count].vertices = vertices;
    logo_shapes[logo_shape_count++].color  = color;
}

void gsKit_prim_line(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color)
{
    TEST_ASSERT_EQUAL_INT(1, z);
    capture_logo(gs, (float[]){ x, right }, (float[]){ y, bottom }, 2, color);
    ++logo_line_count;
}

void gsKit_prim_triangle_gouraud(GSGLOBAL* gs, float x0, float y0, float x1, float y1, float x2, float y2, int z, u64 c0, u64 c1, u64 c2)
{
    TEST_ASSERT_EQUAL_INT(1, z);
    TEST_ASSERT_EQUAL_UINT64(c0, c1);
    TEST_ASSERT_EQUAL_UINT64(c0, c2);
    capture_logo(gs, (float[]){ x0, x1, x2 }, (float[]){ y0, y1, y2 }, 3, c0);
}

void ui_text_scaled(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale)
{
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs->PrimAlphaEnable);
    TEST_ASSERT_EQUAL_UINT(115, (color >> 24) & 255);
    TEST_ASSERT_LESS_THAN_UINT(CAPTURE_ITEMS, label_count);

    labels[label_count].clipped = 0;
    labels[label_count].left    = x;
    labels[label_count].top     = y;
    labels[label_count].scale   = scale;
    labels[label_count].color   = color;

    snprintf(labels[label_count++].text, sizeof(labels[0].text), "%s", s);
}

void ui_text_clipped(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale, float left, float right)
{
    TEST_ASSERT_EQUAL_FLOAT(UI_TRACK_METADATA_LEFT, left);
    TEST_ASSERT_EQUAL_FLOAT(UI_SAFE_RIGHT, right);
    ui_text_scaled(gs, x, y, s, color, scale);

    labels[label_count - 1].clipped = 1;
}

void ui_number(GSGLOBAL* gs, float x, float y, const char* text, float scale)
{
    ui_text_scaled(gs, x, y, text, ui_color(UI_COLOR_TEXT), scale);
}

void setUp(void)
{
    texture_resets = scene_closes = 0;
    scene_count                   = 0;
    display_close_ok              = 1;
    display_open_failure          = 0;
    display_opens = display_closes = 0;
    closed_context                 = NULL;

    memset(&source, 0, sizeof(source));
    memset(&settings, 0, sizeof(settings));
    memset(&drawing, 0, sizeof(drawing));

    source.kind                     = AUDIO_SOURCE_CD;
    source.cd.tracks                = 17;
    source.cd.track                 = 1;
    source.cd.duration_seconds      = 61;
    source.cd.disc_duration_seconds = 1000;

    player_init(&player);

    artwork_available = 1;

    TEST_ASSERT_TRUE(ui_frame_open() == 0);
}

void tearDown(void)
{
    display_close_ok = 1;

    TEST_ASSERT_TRUE(ui_frame_close() == 0);
}

static void frame(UiScreen screen, uint32_t now)
{
    shape_count = label_count = cover_count = 0;
    logo_shape_count = logo_line_count = 0;

    presentation_step     = 0;
    presentation_started  = now;
    presentation_interval = settings.frame_rate == 30 ? 2u : 1u;

#if STROOM_PRESET_BENCHMARK
    timer_reads = 0;
    timer_base += 100;
#endif
    ui_frame_render(screen, &player, &settings, &visualizer, &audio, &source, now);
    TEST_ASSERT_EQUAL_UINT(4, presentation_step);
#if STROOM_PRESET_BENCHMARK
    UiFrameTiming timing = ui_frame_timing();

    TEST_ASSERT_EQUAL_UINT(3, timer_reads);
    TEST_ASSERT_EQUAL_UINT64(timer_base + 11, timing.drawn_ticks);
    TEST_ASSERT_EQUAL_UINT64(timer_base + 29, timing.submitted_ticks);
    TEST_ASSERT_EQUAL_UINT64(timer_base + 53, timing.presented_ticks);
#endif
    TEST_ASSERT_EQUAL_INT(GS_SETTING_OFF, drawing.PrimAlphaEnable);
}

/** @brief Graphics completion precedes decoder scheduling and display presentation. */
static void presentation_order(void)
{
    presentation_started = UINT32_MAX - 10;

    for (int rate = 30; rate <= 60; rate += 30)
    {
        settings.frame_rate = rate;

        frame(UI_SCREEN_CD_PLAYER, presentation_started);
        TEST_ASSERT_EQUAL_UINT(4, presentation_step);
    }
}

static unsigned tiles(void)
{
    unsigned result = 0;

    for (unsigned i = 0; i < shape_count; ++i)
    {
        if (fabsf(shapes[i].right - shapes[i].left - 52) < 0.1f && fabsf(shapes[i].bottom - shapes[i].top - 100 * DISPLAY_HEIGHT / 480.0f) < 0.1f)
        {
            ++result;
        }
    }

    return result;
}

/** Export captured geometry for review; the background represents the separate MilkDrop stage. */
static void snapshot(const char* path)
{
    FILE* file = fopen(path, "w");

    TEST_ASSERT_NOT_NULL(file);
    fprintf(file, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\"><rect width=\"%d\" height=\"%d\" fill=\"#243153\"/>", DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    for (unsigned i = 0; i < shape_count; ++i)
    {
        unsigned rgb = (unsigned)(shapes[i].color & 255) << 16 | (unsigned)((shapes[i].color >> 8) & 255) << 8 | (unsigned)((shapes[i].color >> 16) & 255);

        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"#%06x\" opacity=\"0.9\"/>", shapes[i].left, shapes[i].top, shapes[i].right - shapes[i].left, shapes[i].bottom - shapes[i].top, rgb);
    }

    for (unsigned i = 0; i < label_count; ++i)
    {
        fprintf(file, "<text x=\"%.2f\" y=\"%.2f\" fill=\"#f0f4ff\" font-family=\"monospace\" font-size=\"%.2f\">", labels[i].left, labels[i].top + labels[i].scale * 5, labels[i].scale * 6);

        for (const char* p = labels[i].text; *p; ++p)
        {
            fputs(*p == '&' ? "&amp;" : *p == '<' ? "&lt;"
                                    : *p == '>'   ? "&gt;"
                                                  : (char[2]){ *p, 0 },
                  file);
        }

        fputs("</text>", file);
    }

    for (unsigned i = 0; i < logo_shape_count; ++i)
    {
        unsigned rgb = (unsigned)(logo_shapes[i].color & 255) << 16 | (unsigned)((logo_shapes[i].color >> 8) & 255) << 8 | (unsigned)((logo_shapes[i].color >> 16) & 255);

        if (logo_shapes[i].vertices == 2)
        {
            fprintf(file, "<line x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\" stroke=\"#%06x\" opacity=\"0.9\"/>", logo_shapes[i].x[0], logo_shapes[i].y[0], logo_shapes[i].x[1], logo_shapes[i].y[1], rgb);
        }
        else
        {
            fprintf(file, "<polygon points=\"%.3f,%.3f %.3f,%.3f %.3f,%.3f\" fill=\"#%06x\" opacity=\"0.9\"/>", logo_shapes[i].x[0], logo_shapes[i].y[0], logo_shapes[i].x[1], logo_shapes[i].y[1], logo_shapes[i].x[2], logo_shapes[i].y[2], rgb);
        }
    }

    fputs("</svg>", file);
    fclose(file);
}

static void composition_and_sliding(void)
{
    frame(UI_SCREEN_CD_PLAYER, 0);
    TEST_ASSERT_EQUAL_UINT(1, scene_count);
    TEST_ASSERT_TRUE(scene_overlays);
    TEST_ASSERT_EQUAL_UINT(0, tiles());
    snapshot("cd-preview.svg");

    player.editing = 1;

    frame(UI_SCREEN_CD_PLAYER, 20);
    TEST_ASSERT_EQUAL_UINT(16, tiles());

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (labels[i].top > 130 && labels[i].top < 360)
        {
            TEST_ASSERT_NULL(strchr(labels[i].text, ':'));
        }
    }

    snapshot("program-preview.svg");

    player.editing = 0;

    frame(UI_SCREEN_CD_PLAYER, 40);
    TEST_ASSERT_EQUAL_UINT(0, tiles());

    source.kind = AUDIO_SOURCE_NETWORK;

    strcpy(source.metadata.title, "I SCRUB DUB");
    strcpy(source.metadata.artist, "WAGAWAGA");
    strcpy(source.metadata.album, "B-SIDES AND MIXERS 07-09");
    frame(UI_SCREEN_NETWORK_PLAYER, 60);
    TEST_ASSERT_EQUAL_UINT(1, cover_count);
    TEST_ASSERT_TRUE(captured_cover_top < UI_TRACK_PANEL_TOP);
    TEST_ASSERT_TRUE(UI_PLAYER_COVER_LEFT > UI_SAFE_LEFT + UI_METER_WIDTH);
    TEST_ASSERT_TRUE(captured_cover_top + captured_cover_side <= DISPLAY_HEIGHT);

    unsigned album_labels = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (strcmp(labels[i].text, source.metadata.album) == 0)
        {
            TEST_ASSERT_FLOAT_WITHIN(0.001f, captured_cover_top + captured_cover_side, labels[i].top + UI_TEXT_HEIGHT * labels[i].scale);
            ++album_labels;
        }
    }

    TEST_ASSERT_EQUAL_UINT(1, album_labels);
    snapshot("network-preview.svg");
    frame(UI_SCREEN_VISUALIZER, 60);

    float shown_top = shapes[0].top;

    frame(UI_SCREEN_VISUALIZER, 200);

    float halfway_top = shapes[0].top;

    TEST_ASSERT_TRUE(halfway_top < shown_top);
    TEST_ASSERT_TRUE(captured_cover_top > UI_PLAYER_COVER_TOP);
    frame(UI_SCREEN_NETWORK_PLAYER, 200);
    TEST_ASSERT_EQUAL_FLOAT(halfway_top, shapes[0].top);
    frame(UI_SCREEN_NETWORK_PLAYER, 220);
    TEST_ASSERT_TRUE(shapes[0].top > halfway_top);
    frame(UI_SCREEN_VISUALIZER, 800);
    TEST_ASSERT_EQUAL_UINT(0, shape_count);
    TEST_ASSERT_EQUAL_UINT(0, label_count);
    TEST_ASSERT_FALSE(scene_overlays);

    unsigned previous_scene = scene_count;

    source.network_ready = 1;

    frame(UI_SCREEN_WAITING, 1100);
    TEST_ASSERT_EQUAL_UINT(previous_scene + 1, scene_count);
    TEST_ASSERT_GREATER_THAN_UINT(0, shape_count);
}

/** Text must remain legible and inside the viewport across the longer control states. */
static void readable_text(void)
{
    source.cd.repeat      = CD_REPEAT_ALL;
    player.time_remaining = 1;

    frame(UI_SCREEN_CD_PLAYER, 2000);
    TEST_ASSERT_EQUAL_FLOAT(104 * DISPLAY_HEIGHT / 480.0f, shapes[1].top);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(2.5f, labels[i].scale);
        TEST_ASSERT_TRUE(labels[i].left >= 0);
        TEST_ASSERT_TRUE(labels[i].left + ui_text_width(labels[i].text, labels[i].scale) <= DISPLAY_WIDTH);

        if (!strcmp(labels[i].text, "CD / STOPPED"))
        {
            TEST_ASSERT_TRUE(fabsf(labels[i].top - 41.8f * DISPLAY_HEIGHT / 480.0f) < 0.01f);
        }
    }

    player.editing   = 1;
    player.count     = 1;
    player.tracks[0] = 2;

    frame(UI_SCREEN_CD_PLAYER, 2020);

    int selected = 0;

    for (unsigned i = 0; i < shape_count; ++i)
    {
        if (shapes[i].left == 149 && shapes[i].right == 201 && shapes[i].bottom - shapes[i].top >= 100 * DISPLAY_HEIGHT / 480.0f && shapes[i].color == ui_color(UI_COLOR_SURFACE))
        {
            ++selected;
        }
    }

    TEST_ASSERT_EQUAL_INT(1, selected);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(2.5f, labels[i].scale);

        if (!strcmp(labels[i].text, "02"))
        {
            TEST_ASSERT_EQUAL_HEX64(ui_color(UI_COLOR_TEXT), labels[i].color);
        }
    }

    player.editing = 0;
    source.kind    = AUDIO_SOURCE_NETWORK;

    memset(source.metadata.title, 'X', sizeof(source.metadata.title));
    memset(source.metadata.artist, 'X', sizeof(source.metadata.artist));
    memset(source.metadata.album, 'X', sizeof(source.metadata.album));

    source.metadata.title[sizeof(source.metadata.title) - 1]   = 0;
    source.metadata.artist[sizeof(source.metadata.artist) - 1] = 0;
    source.metadata.album[sizeof(source.metadata.album) - 1]   = 0;

    frame(UI_SCREEN_NETWORK_PLAYER, 2040);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(2.5f, labels[i].scale);
        TEST_ASSERT_TRUE(labels[i].clipped || labels[i].left + ui_text_width(labels[i].text, labels[i].scale) <= UI_SAFE_RIGHT);
    }

    settings.open = 1;

    frame(UI_SCREEN_NETWORK_PLAYER, 2060);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(2.5f, labels[i].scale);
        TEST_ASSERT_TRUE(labels[i].clipped || labels[i].left + ui_text_width(labels[i].text, labels[i].scale) <= UI_SAFE_RIGHT);
    }

    settings.open      = 0;
    settings.show_name = 1;

    frame(UI_SCREEN_VISUALIZER, 2400);
    TEST_ASSERT_GREATER_THAN_UINT(0, label_count);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(2.5f, labels[i].scale);
        TEST_ASSERT_TRUE(labels[i].clipped || labels[i].left + ui_text_width(labels[i].text, labels[i].scale) <= UI_SAFE_RIGHT);
    }
}

/** Enabled fullscreen meters stay at the same positions throughout both transitions. */
static void stationary_enabled_meters(void)
{
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        source.kind = kind ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        UiScreen screen = kind ? UI_SCREEN_NETWORK_PLAYER : UI_SCREEN_CD_PLAYER;
        uint32_t at     = 3000 + kind * 1000;

        settings.show_levels = 1;

        const UiScreen screens[] = { screen, UI_SCREEN_VISUALIZER, UI_SCREEN_VISUALIZER, UI_SCREEN_VISUALIZER, screen, screen, screen };
        const unsigned offsets[] = { 0, 0, 140, 280, 280, 420, 560 };

        for (unsigned step = 0; step < sizeof(screens) / sizeof(screens[0]); ++step)
        {
            frame(screens[step], at + offsets[step]);

            unsigned plates = 0;

            for (unsigned i = 0; i < shape_count; ++i)
            {
                float height = shapes[i].bottom - shapes[i].top;

                if (shapes[i].right - shapes[i].left == 24 && height > 200)
                {
                    TEST_ASSERT_EQUAL_FLOAT(plates ? 568 : 48, shapes[i].left);
                    TEST_ASSERT_EQUAL_FLOAT(120 * DISPLAY_HEIGHT / 480.0f, shapes[i].top);
                    TEST_ASSERT_EQUAL_FLOAT(240 * DISPLAY_HEIGHT / 480.0f, height);
                    ++plates;
                }
            }

            TEST_ASSERT_EQUAL_UINT(2, plates);
            TEST_ASSERT_TRUE(scene_overlays);
        }

        settings.show_levels = 0;

        frame(UI_SCREEN_VISUALIZER, at + 840);
        TEST_ASSERT_EQUAL_UINT(0, shape_count);
        TEST_ASSERT_FALSE(scene_overlays);
        frame(screen, at + 980);

        unsigned moving = 0;

        for (unsigned i = 0; i < shape_count; ++i)
        {
            if (shapes[i].right - shapes[i].left == 24 && shapes[i].bottom - shapes[i].top > 200)
            {
                ++moving;
            }
        }

        TEST_ASSERT_EQUAL_UINT(0, moving);
    }
}

/** Footer controls fit on one line; progress labels share the bar's vertical center. */

/** Waiting instructions retain network availability and address status. */
static void unavailable_sources(void)
{
    for (unsigned state = 0; state < 8; ++state)
    {
        source.network_ready = state & 1;

        snprintf(source.network_address, sizeof(source.network_address), "%s", state & 4 ? "192.168.1.49" : "");
        snprintf(source.cd.error, sizeof(source.cd.error), "%s", state & 2 ? "CD DRIVE FAILED" : "");
        frame(UI_SCREEN_WAITING, 7000 + state * 500);

        const char* expected  = !source.network_ready ? "NETWORK UNAVAILABLE" : !source.network_address[0] ? "GETTING NETWORK ADDRESS"
                                                                                                           : "INSERT DISC OR CAST FROM YOUR DEVICE";
        int         found     = 0;
        unsigned    addresses = 0;

        for (unsigned i = 0; i < label_count; ++i)
        {
            addresses += !strcmp(labels[i].text, "PS2 IP ADDRESS: 192.168.1.49");

            if (!strcmp(labels[i].text, expected))
            {
                found = 1;
            }
        }

        TEST_ASSERT_TRUE_MESSAGE(found, expected);
        TEST_ASSERT_EQUAL_UINT(source.network_ready && source.network_address[0] ? 1 : 0, addresses);
    }
}

/** Shared source geometry, progress colors and a single bottom option row. */

/** Cross visibly toggles membership while the cursor stays on the programmed track. */
static void focused_program_toggle(void)
{
    player.editing = 1;
    player.slot    = 1;

    for (unsigned state = 0; state < 3; ++state)
    {
        if (state)
        {
            player_update(&player, &source.cd, INPUT_CROSS, 0, 1, 10000 + state * 20);
        }

        TEST_ASSERT_EQUAL_INT(state == 1, player.count);
        TEST_ASSERT_EQUAL_INT(1, player.slot);
        frame(UI_SCREEN_CD_PLAYER, 10000 + state * 20);

        unsigned fills = 0, borders = 0;

        for (unsigned i = 0; i < shape_count; ++i)
        {
            if (shapes[i].left == 149 && shapes[i].right == 201 && shapes[i].top >= 128 * DISPLAY_HEIGHT / 480.0f && shapes[i].top < 230 * DISPLAY_HEIGHT / 480.0f)
            {
                if (shapes[i].bottom - shapes[i].top >= 100 * DISPLAY_HEIGHT / 480.0f)
                {
                    TEST_ASSERT_EQUAL_HEX64(ui_color(state == 1 ? UI_COLOR_SURFACE : UI_COLOR_BACKGROUND), shapes[i].color);
                    ++fills;
                }
                else
                {
                    TEST_ASSERT_EQUAL_HEX64(ui_color(state == 1 ? UI_COLOR_ACCENT : UI_COLOR_TEXT), shapes[i].color);
                    TEST_ASSERT_EQUAL_FLOAT(3, shapes[i].bottom - shapes[i].top);
                    ++borders;
                }
            }
        }

        TEST_ASSERT_EQUAL_UINT(1, fills);
        TEST_ASSERT_EQUAL_UINT(2, borders);
    }
}

/** Source changes must preserve panel and meter bounds, including during a slide. */
static void consistent_source_layout(void)
{
    settings.show_levels = 1;

    frame(UI_SCREEN_CD_PLAYER, 11000);

    for (unsigned step = 0; step < 2; ++step)
    {
        uint32_t now    = 11000 + step * 140;
        UiScreen screen = step ? UI_SCREEN_VISUALIZER : UI_SCREEN_CD_PLAYER;

        if (step)
        {
            frame(UI_SCREEN_VISUALIZER, 11000);
        }

        source.kind = AUDIO_SOURCE_CD;

        frame(screen, now);

        float    bounds[6][4] = { 0 };
        unsigned count        = 0;

        for (unsigned i = 0; i < shape_count; ++i)
        {
            if (i < 4 || (shapes[i].right - shapes[i].left == 24 && shapes[i].bottom - shapes[i].top > 200))
            {
                TEST_ASSERT_LESS_THAN_UINT(6, count);

                bounds[count][0]   = shapes[i].left;
                bounds[count][1]   = shapes[i].top;
                bounds[count][2]   = shapes[i].right;
                bounds[count++][3] = shapes[i].bottom;
            }
        }

        TEST_ASSERT_EQUAL_UINT(6, count);

        source.kind = AUDIO_SOURCE_NETWORK;

        frame(step ? UI_SCREEN_VISUALIZER : UI_SCREEN_NETWORK_PLAYER, now);

        count = 0;

        for (unsigned i = 0; i < shape_count; ++i)
        {
            if (i < 4 || (shapes[i].right - shapes[i].left == 24 && shapes[i].bottom - shapes[i].top > 200))
            {
                TEST_ASSERT_LESS_THAN_UINT(6, count);
                TEST_ASSERT_EQUAL_FLOAT(bounds[count][0], shapes[i].left);
                TEST_ASSERT_EQUAL_FLOAT(bounds[count][1], shapes[i].top);
                TEST_ASSERT_EQUAL_FLOAT(bounds[count][2], shapes[i].right);
                TEST_ASSERT_EQUAL_FLOAT(bounds[count][3], shapes[i].bottom);
                ++count;
            }
        }

        TEST_ASSERT_EQUAL_UINT(6, count);
        TEST_ASSERT_EQUAL_UINT(1, cover_count);
        TEST_ASSERT_TRUE(captured_cover_top < shapes[3].top);
        TEST_ASSERT_TRUE(captured_cover_top + captured_cover_side > shapes[3].bottom);
        TEST_ASSERT_TRUE(captured_cover_top + captured_cover_side < shapes[2].bottom);

        for (unsigned i = 0; i < label_count; ++i)
        {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[i].text, "NOW STREAMING"));
        }
    }
}

/** Playback state changes update the centered source status immediately. */
static void playback_status_header(void)
{
    const char* expected[] = { "CD / STOPPED", "CD / PLAYING", "CD / PAUSED", "CD / PLAYING", "CD / STOPPED", "NETWORK / STREAMING", "CD / PLAYING / MUTED", "NETWORK / STREAMING / MUTED", "NETWORK / LISTENING" };

    for (unsigned state = 0; state < sizeof(expected) / sizeof(expected[0]); ++state)
    {
        source.kind       = state == 5 || state >= 7 ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;
        settings.muted    = state >= 6;
        source.listening  = state == 8;
        source.cd.playing = state == 1 || state == 3 || state == 6;
        source.cd.paused  = state == 2;

        frame(source.kind == AUDIO_SOURCE_NETWORK ? UI_SCREEN_NETWORK_PLAYER : UI_SCREEN_CD_PLAYER, 12000 + state * 20);
        TEST_ASSERT_EQUAL_UINT(0, logo_shape_count);

        unsigned found = 0;

        for (unsigned i = 0; i < label_count; ++i)
        {
            if (!strcmp(labels[i].text, expected[state]))
            {
                TEST_ASSERT_FLOAT_WITHIN(0.01f, DISPLAY_WIDTH / 2.0f, labels[i].left + ui_text_width(labels[i].text, labels[i].scale) / 2);
                ++found;
            }
        }

        TEST_ASSERT_EQUAL_UINT(1, found);
    }
}

/** Errors replace source status without exposing backend messages, and clear on recovery. */
static void error_status_header(void)
{
    const UiScreen screens[]   = { UI_SCREEN_CD_PLAYER, UI_SCREEN_CD_PLAYER, UI_SCREEN_CD_PLAYER, UI_SCREEN_NETWORK_PLAYER, UI_SCREEN_WAITING, UI_SCREEN_WAITING, UI_SCREEN_DETECTING };
    const char*    recovered[] = { "CD / STOPPED", "CD / PLAYING", "CD / PAUSED", "NETWORK / STREAMING", NULL, NULL, NULL };

    for (unsigned state = 0; state < sizeof(screens) / sizeof(screens[0]); ++state)
    {
        int cd_error = state < 3 || state == 4 || state == 6;

        source.kind       = cd_error ? AUDIO_SOURCE_CD : AUDIO_SOURCE_NETWORK;
        source.cd.playing = state == 1;
        source.cd.paused  = state == 2;

        snprintf(source.cd.error, sizeof(source.cd.error), "%s", cd_error ? "CD BACKEND FAILURE" : "");
        snprintf(source.error, sizeof(source.error), "%s", cd_error ? "" : "NETWORK BACKEND FAILURE");

        for (unsigned recovery = 0; recovery < 2; ++recovery)
        {
            if (recovery)
            {
                source.cd.error[0] = source.error[0] = 0;
            }

            frame(screens[state], 13000 + state * 100 + recovery * 20);

            unsigned    found    = 0;
            const char* expected = recovery ? recovered[state] : cd_error ? "CD / ERROR"
                                                                          : "NETWORK / ERROR";

            for (unsigned i = 0; i < label_count; ++i)
            {
                TEST_ASSERT_NULL(strstr(labels[i].text, "BACKEND FAILURE"));

                if (expected && !strcmp(labels[i].text, expected))
                {
                    ++found;
                }
            }

            TEST_ASSERT_EQUAL_UINT(expected ? 1 : 0, found);
            TEST_ASSERT_EQUAL_UINT(expected ? 0 : 1, logo_shape_count > 0);
        }
    }

    for (unsigned kind = 0; kind < 2; ++kind)
    {
        source.kind = kind ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        strcpy(source.cd.error, "CD BACKEND FAILURE");
        strcpy(source.error, "NETWORK BACKEND FAILURE");
        frame(UI_SCREEN_VISUALIZER, 14000 + kind * 400);
        frame(UI_SCREEN_VISUALIZER, 14300 + kind * 400);
        TEST_ASSERT_EQUAL_UINT(0, label_count);
    }
}

/** @brief Empty CD layouts show generic status without backend details. */
static void cd_preparation_status(void)
{
    source.cd.tracks = 0;

    strcpy(source.cd.status, "READING CD TRACKS - ATTEMPT 1");
    frame(UI_SCREEN_CD_PLAYER, 15000);

    unsigned progress = 0, loading = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        progress += !strcmp(labels[i].text, source.cd.status);
        loading += !strcmp(labels[i].text, "CD / LOADING");
    }

    TEST_ASSERT_EQUAL_UINT(0, progress);
    TEST_ASSERT_EQUAL_UINT(1, loading);
    strcpy(source.cd.error, "CD TRACK LIST ERROR - SELECT PLAY TO RETRY");
    frame(UI_SCREEN_CD_PLAYER, 15100);

    unsigned errors = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        errors += !strcmp(labels[i].text, "CD / ERROR");

        TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[i].text, source.cd.error));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[i].text, source.cd.status));
    }

    TEST_ASSERT_EQUAL_UINT(1, errors);

    source.cd.tracks   = 17;
    source.cd.error[0] = 0;

    frame(UI_SCREEN_CD_PLAYER, 15200);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[i].text, source.cd.status));
    }
}

static void metadata_font_and_truncation(void)
{
    source.kind = AUDIO_SOURCE_NETWORK;

    strcpy(source.metadata.title, "A$@=B");
    strcpy(source.metadata.artist, "caf\xc3\xa9 / \xe2\x99\xab");

    source.metadata.album[0] = 0;

    frame(UI_SCREEN_NETWORK_PLAYER, 16000);

    unsigned found = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "A???B") || !strcmp(labels[i].text, "cafe / ?"))
        {
            ++found;
        }
    }

    TEST_ASSERT_EQUAL_UINT(2, found);
    TEST_ASSERT_EQUAL_STRING("A$@=B", source.metadata.title);
    strcpy(source.metadata.title, "012345678901\xc3\xa9"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    frame(UI_SCREEN_NETWORK_PLAYER, 16020);

    found = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "012345678901eABCDEFGHIJKLMNOPQRSTUVWXYZ"))
        {
            ++found;
            TEST_ASSERT_EQUAL_INT(ui_text_width(labels[i].text, labels[i].scale) > UI_SAFE_RIGHT - UI_TRACK_METADATA_LEFT, labels[i].clipped);
        }
    }

    TEST_ASSERT_EQUAL_UINT(1, found);
}

static void punctuation_metadata(void)
{
    for (int network = 0; network < 2; ++network)
    {
        source.kind = network ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        strcpy(source.metadata.title, "I’m So Excited");
        strcpy(source.metadata.artist, "‘Guest’ – Artist");
        strcpy(source.metadata.album, "“Album” — «Live»");

        TrackMetadata original = source.metadata;

        frame(network ? UI_SCREEN_NETWORK_PLAYER : UI_SCREEN_CD_PLAYER, 15000);

        unsigned found = 0;

        for (unsigned i = 0; i < label_count; ++i)
        {
            if (!strcmp(labels[i].text, "I'm So Excited") ||
                !strcmp(labels[i].text, "'Guest' - Artist") ||
                !strcmp(labels[i].text, "\"Album\" - \"Live\""))
            {
                ++found;
            }
        }

        TEST_ASSERT_EQUAL_UINT(3, found);
        TEST_ASSERT_EQUAL_MEMORY(&original, &source.metadata, sizeof(original));
    }
}

static void latin_metadata(void)
{
    source.kind = AUDIO_SOURCE_CD;

    strcpy(source.metadata.title, "Prva pomoć ščćžđ ŠČĆŽĐ");
    strcpy(source.metadata.artist, "Ivana Brkić");
    frame(UI_SCREEN_CD_PLAYER, 16040);

    unsigned found = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "Prva pomoc scczd SCCZD") ||
            !strcmp(labels[i].text, "Ivana Brkic"))
        {
            ++found;
        }
    }

    TEST_ASSERT_EQUAL_UINT(2, found);
    TEST_ASSERT_EQUAL_STRING("Ivana Brkić", source.metadata.artist);
}

/** Listening has no track widgets, even if a sender supplies labels and a cover URL. */
static void listening_omits_track_content(void)
{
    settings.show_levels = 1;
    source.kind          = AUDIO_SOURCE_NETWORK;

    strcpy(source.metadata.title, "TRACK TITLE");
    strcpy(source.metadata.artist, "ARTIST");
    strcpy(source.metadata.album, "ALBUM");
    strcpy(source.metadata.artwork_url, "http://example.test/cover.jpg");
    frame(UI_SCREEN_NETWORK_PLAYER, 17000);
    TEST_ASSERT_EQUAL_UINT(1, cover_count);

    source.listening = 1;

    for (unsigned empty = 0; empty < 2; ++empty)
    {
        if (empty)
        {
            memset(&source.metadata, 0, sizeof(source.metadata));
        }

        frame(UI_SCREEN_NETWORK_PLAYER, 17020 + empty * 20);
        TEST_ASSERT_EQUAL_UINT(0, cover_count);
        TEST_ASSERT_EQUAL_UINT(3, label_count);
        TEST_ASSERT_EQUAL_STRING("NETWORK / LISTENING", labels[0].text);
        TEST_ASSERT_EQUAL_STRING("L", labels[1].text);
        TEST_ASSERT_EQUAL_STRING("R", labels[2].text);
        TEST_ASSERT_EQUAL_FLOAT(0, shapes[2].left);
        TEST_ASSERT_EQUAL_FLOAT(DISPLAY_WIDTH, shapes[2].right);
        TEST_ASSERT_EQUAL_FLOAT(UI_FOOTER_TOP * DISPLAY_HEIGHT / UI_REFERENCE_HEIGHT, shapes[2].top);
        TEST_ASSERT_EQUAL_FLOAT(DISPLAY_HEIGHT, shapes[2].bottom);
        TEST_ASSERT_EQUAL_HEX64(ui_color(UI_COLOR_BACKGROUND), shapes[2].color);

        for (unsigned i = 0; i < shape_count; ++i)
        {
            TEST_ASSERT_FALSE(shapes[i].left == UI_PLAYER_COVER_LEFT &&
                              shapes[i].right - shapes[i].left == UI_PLAYER_COVER_SIDE);
        }
    }

    source.listening = 0;

    frame(UI_SCREEN_NETWORK_PLAYER, 17080);
    TEST_ASSERT_EQUAL_UINT(1, cover_count);
}

/** Device identity is centered and bounded; it is independent of track labels. */
static void listening_device_label(void)
{
    source.kind      = AUDIO_SOURCE_NETWORK;
    source.listening = 1;

    strcpy(source.device_name, "Focusrite 18i20");

    for (unsigned long_name = 0; long_name < 2; ++long_name)
    {
        if (long_name)
        {
            memset(source.device_name, 'X', sizeof(source.device_name) - 1);

            source.device_name[sizeof(source.device_name) - 1] = 0;
        }

        frame(UI_SCREEN_NETWORK_PLAYER, 18000 + long_name * 20);
        TEST_ASSERT_EQUAL_UINT(0, cover_count);

        unsigned found = 0;

        for (unsigned i = 0; i < label_count; ++i)
        {
            if (strncmp(labels[i].text, "LISTENING FROM: ", strlen("LISTENING FROM: ")) != 0)
            {
                continue;
            }

            ++found;

            if (!long_name)
            {
                TEST_ASSERT_EQUAL_STRING("LISTENING FROM: Focusrite 18i20", labels[i].text);
            }
            else
            {
                TEST_ASSERT_NOT_NULL(strstr(labels[i].text, "..."));
            }

            float width = ui_text_width(labels[i].text, labels[i].scale);

            TEST_ASSERT_FLOAT_WITHIN(0.01f, DISPLAY_WIDTH / 2.0f, labels[i].left + width / 2);
            TEST_ASSERT_TRUE(labels[i].left >= UI_SAFE_LEFT);
            TEST_ASSERT_TRUE(labels[i].left + width <= UI_SAFE_RIGHT);
        }

        TEST_ASSERT_EQUAL_UINT(1, found);
    }
}

static void cd_recognized_track_content(void)
{
    strcpy(source.metadata.title, "CD TITLE");
    strcpy(source.metadata.artist, "CD ARTIST");
    strcpy(source.metadata.album, "CD ALBUM");
    frame(UI_SCREEN_CD_PLAYER, 19000);
    frame(UI_SCREEN_CD_PLAYER, 19300);

    unsigned found = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "CD TITLE") || !strcmp(labels[i].text, "CD ARTIST") || !strcmp(labels[i].text, "CD ALBUM"))
        {
            ++found;
        }
    }

    TEST_ASSERT_EQUAL_UINT(3, found);
    TEST_ASSERT_EQUAL_UINT(1, cover_count);

    player.editing = 1;

    frame(UI_SCREEN_CD_PLAYER, 19320);
    TEST_ASSERT_EQUAL_UINT(0, cover_count);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_TRUE(strcmp(labels[i].text, "CD TITLE") != 0);
    }
}

static void missing_track_content(void)
{
    artwork_available = 0;

    for (unsigned network = 0; network < 2; ++network)
    {
        source.kind = network ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        UiScreen screen = network ? UI_SCREEN_NETWORK_PLAYER : UI_SCREEN_CD_PLAYER;

        frame(screen, 18000 + network * 400);
        frame(screen, 18300 + network * 400);
        TEST_ASSERT_EQUAL_UINT(0, cover_count);

        for (unsigned i = 0; i < label_count; ++i)
        {
            TEST_ASSERT_TRUE(strcmp(labels[i].text, "-") != 0);
        }

        for (unsigned i = 0; i < shape_count; ++i)
        {
            if (shapes[i].top > UI_FOOTER_TOP * DISPLAY_HEIGHT / UI_REFERENCE_HEIGHT)
            {
                TEST_FAIL_MESSAGE("Empty footer contains artwork decoration");
            }
        }
    }
}

static void remaining_time_prefix_preserves_digits(void)
{
    source.cd.elapsed_seconds = 0;

    float digit_left = 0, digit_top = 0, digit_scale = 0;

    frame(UI_SCREEN_CD_PLAYER, 20000);

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "00:00"))
        {
            digit_left  = labels[i].left;
            digit_top   = labels[i].top;
            digit_scale = labels[i].scale;
        }
    }

    TEST_ASSERT_TRUE(digit_scale > 0);

    unsigned track_labels = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "01"))
        {
            TEST_ASSERT_EQUAL_FLOAT(digit_top, labels[i].top);
            TEST_ASSERT_EQUAL_FLOAT(digit_scale, labels[i].scale);
            ++track_labels;
        }
    }

    TEST_ASSERT_EQUAL_UINT(1, track_labels);

    unsigned status_labels = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strncmp(labels[i].text, "CD / ", 5))
        {
            TEST_ASSERT_FLOAT_WITHIN(0.01f, labels[i].top + UI_TEXT_HEIGHT * labels[i].scale, digit_top + UI_TEXT_HEIGHT * digit_scale);
            ++status_labels;
        }
    }

    TEST_ASSERT_EQUAL_UINT(1, status_labels);

    player.time_remaining = 1;

    frame(UI_SCREEN_CD_PLAYER, 20020);

    unsigned found = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (labels[i].scale != digit_scale)
        {
            continue;
        }

        if (!strcmp(labels[i].text, "01:01"))
        {
            TEST_ASSERT_EQUAL_FLOAT(digit_left, labels[i].left);
            TEST_ASSERT_EQUAL_FLOAT(digit_top, labels[i].top);
            ++found;
        }

        if (!strcmp(labels[i].text, "-"))
        {
            TEST_ASSERT_EQUAL_FLOAT(digit_left - UI_TEXT_ADVANCE * digit_scale, labels[i].left);
            TEST_ASSERT_EQUAL_FLOAT(digit_top, labels[i].top);
            ++found;
        }
    }

    TEST_ASSERT_EQUAL_UINT(2, found);
}

static void source_specific_playback_menu(void)
{
    source.kind   = AUDIO_SOURCE_NETWORK;
    settings.open = 1;
    settings.page = MENU_PLAYBACK;

    frame(UI_SCREEN_NETWORK_PLAYER, 21000);

    unsigned sounds = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, "SOUND: ON"))
        {
            ++sounds;
        }

        TEST_ASSERT_NULL(strstr(labels[i].text, "TIME MODE:"));
        TEST_ASSERT_NULL(strstr(labels[i].text, "PROGRAM TRACKS"));
        TEST_ASSERT_NULL(strstr(labels[i].text, "MODE: CONTINUE"));
    }

    TEST_ASSERT_EQUAL_UINT(1, sounds);

    settings.page    = MENU_ROOT;
    source.listening = 1;

    frame(UI_SCREEN_NETWORK_PLAYER, 21020);

    for (unsigned i = 0; i < label_count; ++i)
    {
        TEST_ASSERT_TRUE(strcmp(labels[i].text, "PLAYBACK") != 0);
        TEST_ASSERT_NULL(strstr(labels[i].text, "SOUND:"));
    }
}

static void scrolling_metadata(void)
{
    source.kind = AUDIO_SOURCE_NETWORK;

    strcpy(source.metadata.title, "ABCDEFGHIJKLMNOPQRSTUVWXYZ ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    strcpy(source.metadata.artist, "Short artist");

    float first = 0, moved = 0;

    for (uint32_t now = 30000; now <= 32600; now += 100)
    {
        frame(UI_SCREEN_NETWORK_PLAYER, now);

        for (unsigned i = 0; i < label_count; ++i)
        {
            if (!strcmp(labels[i].text, source.metadata.title))
            {
                TEST_ASSERT_TRUE(labels[i].clipped);

                if (now == 30000)
                {
                    first = labels[i].left;
                }

                if (now <= 31800)
                {
                    TEST_ASSERT_EQUAL_FLOAT(first, labels[i].left);
                }

                moved = labels[i].left;
            }

            if (!strcmp(labels[i].text, "Short artist"))
            {
                TEST_ASSERT_EQUAL_FLOAT(UI_TRACK_METADATA_LEFT, labels[i].left);
            }
        }
    }

    TEST_ASSERT_TRUE(moved < first);
    strcpy(source.metadata.title, "NEW ABCDEFGHIJKLMNOPQRSTUVWXYZ ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    frame(UI_SCREEN_NETWORK_PLAYER, 32700);

    for (unsigned i = 0; i < label_count; ++i)
    {
        if (!strcmp(labels[i].text, source.metadata.title))
        {
            TEST_ASSERT_EQUAL_FLOAT(UI_TRACK_METADATA_LEFT, labels[i].left);
        }
    }
}

static void display_close_failure_preserves_context(void)
{
    display_close_ok = 0;

    TEST_ASSERT_TRUE(!(ui_frame_close() == 0));
    TEST_ASSERT_EQUAL_UINT(0, texture_resets);
    TEST_ASSERT_EQUAL_UINT(0, scene_closes);
    TEST_ASSERT_EQUAL_PTR(&drawing, closed_context);
    TEST_ASSERT_TRUE(!(ui_frame_open() == 0));
    TEST_ASSERT_EQUAL_UINT(1, display_opens);

    display_close_ok = 1;

    TEST_ASSERT_TRUE(ui_frame_close() == 0);
    TEST_ASSERT_EQUAL_UINT(1, texture_resets);
    TEST_ASSERT_EQUAL_UINT(1, scene_closes);
    TEST_ASSERT_EQUAL_PTR(&drawing, closed_context);
    TEST_ASSERT_TRUE(ui_frame_close() == 0);
    TEST_ASSERT_NULL(closed_context);
    TEST_ASSERT_TRUE(ui_frame_open() == 0);
    TEST_ASSERT_EQUAL_UINT(2, display_opens);
}

static void failed_startup_cleanup_retries(void)
{
    TEST_ASSERT_TRUE(ui_frame_close() == 0);

    display_open_failure = 1;

    TEST_ASSERT_TRUE(!(ui_frame_open() == 0));

    display_close_ok = 0;

    TEST_ASSERT_TRUE(!(ui_frame_close() == 0));
    TEST_ASSERT_NULL(closed_context);
    TEST_ASSERT_TRUE(!(ui_frame_open() == 0));
    TEST_ASSERT_EQUAL_UINT(2, display_opens);

    display_close_ok = 1;

    TEST_ASSERT_TRUE(ui_frame_close() == 0);

    display_open_failure = 0;

    TEST_ASSERT_TRUE(ui_frame_open() == 0);
    TEST_ASSERT_EQUAL_UINT(3, display_opens);
}

/** @brief A source failure explanation is reported once, with a neutral header. */
static void failure_reason_is_not_duplicated(void)
{
    source = (AudioSourceStatus){ .kind = AUDIO_SOURCE_ERROR };

    strcpy(source.error, "SDK CLEANUP FAILED");
    frame(UI_SCREEN_WAITING, 25000);

    unsigned errors = 0, headers = 0;

    for (unsigned i = 0; i < label_count; ++i)
    {
        errors += !strcmp(labels[i].text, "SDK CLEANUP FAILED");
        headers += !strcmp(labels[i].text, APP_NAME);

        TEST_ASSERT_TRUE(strcmp(labels[i].text, "ERROR") != 0);
    }

    TEST_ASSERT_EQUAL_UINT(1, errors);
    TEST_ASSERT_EQUAL_UINT(0, headers);
    TEST_ASSERT_GREATER_THAN_UINT(0, logo_line_count);
    TEST_ASSERT_GREATER_THAN_UINT(logo_line_count, logo_shape_count);
}

/** The complete wordmark fits the waiting header and excludes the tagline. */
static void waiting_logo_fits_header(void)
{
    const UiScreen screens[] = { UI_SCREEN_WAITING, UI_SCREEN_DETECTING };

    for (unsigned state = 0; state < 2; ++state)
    {
        frame(screens[state], 26000 + state * 20);
        TEST_ASSERT_GREATER_THAN_UINT(0, logo_line_count);
        TEST_ASSERT_GREATER_THAN_UINT(logo_line_count, logo_shape_count);

        float left = DISPLAY_WIDTH, right = 0;

        for (unsigned i = 0; i < logo_shape_count; ++i)
        {
            for (unsigned vertex = 0; vertex < logo_shapes[i].vertices; ++vertex)
            {
                float x = logo_shapes[i].x[vertex], y = logo_shapes[i].y[vertex];

                TEST_ASSERT_TRUE(x >= UI_SAFE_LEFT && x <= UI_SAFE_RIGHT);
                TEST_ASSERT_TRUE(y > 0 && y < ui_player_y(UI_HEADER_HEIGHT));

                left  = fminf(left, x);
                right = fmaxf(right, x);
            }
        }

        TEST_ASSERT_FLOAT_WITHIN(0.1f, DISPLAY_WIDTH * 0.5f, (left + right) * 0.5f);
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 280.0f, right - left);

        for (unsigned i = 0; i < label_count; ++i)
        {
            TEST_ASSERT_TRUE(labels[i].top > ui_player_y(UI_FOOTER_TOP));
            TEST_ASSERT_NULL(strstr(labels[i].text, APP_NAME));
            TEST_ASSERT_NULL(strstr(labels[i].text, "visualiser"));
        }

        if (!state)
        {
            snapshot("waiting-preview.svg");
        }
    }
}

/** Every part of the logo follows the header when it slides into view. */
static void waiting_logo_follows_header(void)
{
    frame(UI_SCREEN_VISUALIZER, 27000);
    TEST_ASSERT_EQUAL_UINT(0, logo_shape_count);
    frame(UI_SCREEN_WAITING, 27140);
    TEST_ASSERT_GREATER_THAN_UINT(0, logo_shape_count);

    float partial_top = logo_shapes[0].y[0];

    frame(UI_SCREEN_WAITING, 27280);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (ui_player_y(UI_HEADER_HEIGHT) + UI_PANEL_BORDER_HEIGHT) * 0.5f, logo_shapes[0].y[0] - partial_top);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(waiting_logo_fits_header);
    RUN_TEST(waiting_logo_follows_header);
    RUN_TEST(failure_reason_is_not_duplicated);
    RUN_TEST(display_close_failure_preserves_context);
    RUN_TEST(failed_startup_cleanup_retries);
    RUN_TEST(presentation_order);
    RUN_TEST(composition_and_sliding);
    RUN_TEST(readable_text);
    RUN_TEST(focused_program_toggle);
    RUN_TEST(stationary_enabled_meters);
    RUN_TEST(unavailable_sources);
    RUN_TEST(consistent_source_layout);
    RUN_TEST(playback_status_header);
    RUN_TEST(error_status_header);
    RUN_TEST(cd_preparation_status);
    RUN_TEST(metadata_font_and_truncation);
    RUN_TEST(latin_metadata);
    RUN_TEST(punctuation_metadata);
    RUN_TEST(missing_track_content);
    RUN_TEST(cd_recognized_track_content);
    RUN_TEST(listening_omits_track_content);
    RUN_TEST(listening_device_label);
    RUN_TEST(remaining_time_prefix_preserves_digits);
    RUN_TEST(source_specific_playback_menu);
    RUN_TEST(scrolling_metadata);

    return UNITY_END();
}

GSGLOBAL* platform_display_open(void)
{
    ++display_opens;

    return display_open_failure ? NULL : &drawing;
}

int platform_display_close(GSGLOBAL* gs)
{
    TEST_ASSERT_TRUE(gs == NULL || gs == &drawing);

    closed_context = gs;

    ++display_closes;

    return display_close_ok ? 0 : -1;
}

void platform_display_submit(GSGLOBAL* gs)
{
    gsKit_queue_exec(gs);
    gsKit_finish();
}

void gsKit_clear(GSGLOBAL* gs, u64 color)
{
    (void)gs;
    (void)color;
}
