#include "ui/frame/frame.h"
#include "contracts/milkdrop.h"
#include "platform/time/clock.h"
#include "platform/graphics/display.h"
#include "ui/artwork/view.h"
#include "ui/shared/draw.h"
#include "ui/shared/track_layout.h"
#include "ui/shared/track_view.h"
#include "ui/motion.h"
#include "ui/shared/level_meter.h"
#include "ui/shared/player_view.h"
#include "ui/cd_player/view.h"
#include "ui/network_player/view.h"
#include "ui/waiting/view.h"
#include "ui/settings/view.h"
#include "ui/visualizer/scene.h"
#include "ui/visualizer/overlays.h"

#if STROOM_PRESET_BENCHMARK
#include "ui/frame/benchmark.h"
#include "ui/shared/text.h"

static UiFrameTiming timing;
#endif

#define BUTTON_SCALE 2.75f

static UiMotion  motion;
static GSGLOBAL* graphics;
static int       closing;

typedef struct
{
    float top, bottom;
} PanelOffsets;

static PanelOffsets panels(GSGLOBAL* gs, float visible)
{
    PanelOffsets offsets = {
        -(ui_player_y(UI_HEADER_HEIGHT) + UI_PANEL_BORDER_HEIGHT) * (1 - visible),
        (DISPLAY_HEIGHT - ui_player_y(UI_FOOTER_TOP) + UI_PANEL_BORDER_HEIGHT + UI_PLAYER_COVER_OVERLAP) * (1 - visible)
    };

    ui_rectangle(gs, 0, offsets.top, DISPLAY_WIDTH, ui_player_y(UI_HEADER_HEIGHT), ui_color(UI_COLOR_BACKGROUND));
    ui_rectangle(gs, 0, ui_player_y(UI_HEADER_HEIGHT) + offsets.top, DISPLAY_WIDTH, UI_PANEL_BORDER_HEIGHT, ui_color(UI_COLOR_TEXT));
    ui_rectangle(gs, 0, ui_player_y(UI_FOOTER_TOP) + offsets.bottom, DISPLAY_WIDTH, DISPLAY_HEIGHT - ui_player_y(UI_FOOTER_TOP), ui_color(UI_COLOR_BACKGROUND));
    ui_rectangle(gs, 0, ui_player_y(UI_FOOTER_TOP) + offsets.bottom, DISPLAY_WIDTH, UI_PANEL_BORDER_HEIGHT, ui_color(UI_COLOR_TEXT));

    return offsets;
}

static void draw(GSGLOBAL* gs, UiScreen screen, const PlayerState* player, const AppSettings* settings, MilkdropRuntime* visualizer, const Audio* audio, const AudioSourceStatus* source, uint32_t now_ms)
{
    float visible = ui_motion_step(&motion, screen != UI_SCREEN_VISUALIZER, now_ms);
    int   active  = audio->active && (source->kind != AUDIO_SOURCE_CD || (source->cd.playing && !source->cd.scanning));

    ui_level_update(audio->rms, active, now_ms);
    scene_draw(gs, &visualizer->director, &visualizer->music, visualizer->feedback, visible > 0 || settings->open || settings->show_name || settings->show_levels);

    int old_enable = gs->PrimAlphaEnable;
    u64 old_alpha  = gs->PrimAlpha;

    gs->PrimAlphaEnable = GS_SETTING_ON;

    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    if (visible > 0)
    {
        int          waiting = screen == UI_SCREEN_WAITING || screen == UI_SCREEN_DETECTING;
        PanelOffsets offsets = panels(gs, visible);

        ui_player_source_draw(gs, source, waiting, settings->muted, offsets.top);

        if (waiting)
        {
            ui_waiting_draw(gs, source, screen, offsets.bottom);
        }
        else if (source->kind == AUDIO_SOURCE_CD)
        {
            ui_cd_player_draw(gs, &source->cd, player, offsets.top, offsets.bottom, visible);
        }
        else if (source->listening)
        {
            ui_network_listening_draw(gs, source->device_name, offsets.bottom);
        }

        if (!waiting && (source->kind == AUDIO_SOURCE_CD ? !player->editing : !source->listening))
        {
            ui_track_details_draw(gs, &source->metadata, offsets.bottom, now_ms);
        }
    }

    int player_screen = screen != UI_SCREEN_WAITING && screen != UI_SCREEN_DETECTING;

    if (player_screen && settings->show_levels)
    {
        ui_player_meters_draw(gs);
    }

    if (screen == UI_SCREEN_VISUALIZER && visible == 0)
    {
        if (settings->show_name)
        {
            ui_preset_name_scaled(gs, &visualizer->director, BUTTON_SCALE);
        }
    }

    if (settings->open)
    {
        ui_settings_draw(gs, settings, &visualizer->director, player, source->kind == AUDIO_SOURCE_CD, source->listening);
    }

    gsKit_set_primalpha(gs, old_alpha, 0);

    gs->PrimAlphaEnable = old_enable;
}

static void present(GSGLOBAL* gs, int frame_rate, uint32_t frame_started)
{
    platform_display_submit(gs);
#if STROOM_PRESET_BENCHMARK
    timing.submitted_ticks = platform_ticks();
#endif
    ui_artwork_yield(frame_started);

    unsigned refresh_interval = frame_rate == MILKDROP_FPS_BASELINE ? MILKDROP_FPS_HIGH / MILKDROP_FPS_BASELINE : 1u;

    platform_display_present(gs, refresh_interval);
#if STROOM_PRESET_BENCHMARK
    timing.presented_ticks = platform_ticks();
#endif
}

int ui_frame_open(void)
{
    if (closing)
    {
        return UI_FRAME_ERROR_ALREADY_OPEN;
    }

    if (graphics)
    {
        return 0;
    }

    graphics = platform_display_open();
    motion   = (UiMotion){ 0 };
    closing  = graphics == NULL;

    return (graphics != NULL) ? 0 : UI_FRAME_ERROR_DISPLAY_OPEN;
}

int ui_frame_close(void)
{
    closing = 1;

    int result = platform_display_close(graphics);

    if (result != 0)
    {
        return result < 0 ? UI_FRAME_ERROR_DISPLAY_CLOSE : result;
    }

    if (graphics)
    {
        scene_close();
        ui_artwork_reset_texture();
    }

    graphics = NULL;
    closing  = 0;

    return 0;
}

void ui_frame_render(UiScreen screen, const PlayerState* player, const AppSettings* settings, MilkdropRuntime* visualizer, const Audio* audio, const AudioSourceStatus* source, uint32_t now_ms)
{
    draw(graphics, screen, player, settings, visualizer, audio, source, now_ms);
#if STROOM_PRESET_BENCHMARK
    timing.drawn_ticks = platform_ticks();
#endif
    present(graphics, settings->frame_rate, now_ms);
}

#if STROOM_PRESET_BENCHMARK
GSGLOBAL* ui_frame_benchmark_context(void)
{
    return graphics;
}

void ui_frame_benchmark_message(const char* const* lines, unsigned count)
{
    gsKit_clear(graphics, GS_SETREG_RGBAQ(0, 0, 0, 128, 0));

    for (unsigned line = 0; line < count; ++line)
    {
        float x = (graphics->Width - ui_text_width(lines[line], 2.0f)) * 0.5f;

        ui_text_scaled(graphics, x, graphics->Height * 0.5f - 32.0f + line * 32.0f, lines[line], GS_SETREG_RGBAQ(128, 128, 128, 128, 0), 2.0f);
    }

    platform_display_submit(graphics);
    platform_display_present(graphics, 1u);
}

UiFrameTiming ui_frame_timing(void)
{
    return timing;
}
#endif
