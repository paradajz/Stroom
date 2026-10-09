#pragma once

#include "ui/screen.h"
#include "app/config.h"
#include "ui/settings/controller.h"
#include "audio/source/source.h"
#include "milkdrop/runtime.h"
#include "ui/cd_player/controller.h"

/**
 * @brief Application-owned controllers and source-transition bookkeeping.
 */
typedef struct
{
    AppSettings     settings;            /**< Settings menu and overlay options. */
    PlayerState     player;              /**< CD player navigation, program draft, and gestures. */
    int             visualizer_started;  /**< Nonzero after the initial visualization update. */
    MilkdropRuntime visualizer;          /**< Audio analysis and preset playback state. */
    AudioSourceKind previous_source;     /**< Source observed on the preceding update. */
    unsigned        previous_network;    /**< Last observed active network stream generation token. */
    unsigned        previous_disc;       /**< Last observed CD generation counter. */
    int             was_network_waiting; /**< Network-wait state observed on the previous frame. */
    int             listening_intro;     /**< Pending automatic fullscreen transition for this session. */
    uint32_t        listening_since;     /**< Start of the listening introduction in milliseconds. */
    uint32_t        last_frame;          /**< Previous frame timestamp in monotonic milliseconds. */
} AppState;

/**
 * @brief Effects returned by the controller for the application runtime to dispatch.
 */
typedef struct
{
    AudioTransportRequests transport;   /**< Transport requests for the selected source. */
    int                    reset_scene; /**< Nonzero to clear feedback history. */
} AppActions;

/**
 * @brief Initialize application, player, and visualizer state.
 *
 * @param app Application to initialize.
 * @param seed Preset random seed.
 * @param now Current monotonic time in milliseconds.
 * @param config Startup display and sound preferences.
 */
void app_init(AppState* app, uint32_t seed, uint32_t now, const AppConfig* config);

/**
 * @brief Choose the screen from source readiness and player visibility.
 *
 * @param app Application state.
 * @param source Current source snapshot.
 * @return Screen to display.
 */
UiScreen app_screen(const AppState* app, const AudioSourceStatus* source);

/**
 * @brief Advance application state and route input to its current owner. No hardware calls are made.
 *
 * @param app Application to update.
 * @param audio Current audio snapshot.
 * @param source Current source snapshot.
 * @param input Held buttons, press edges, and connection state.
 * @param now Current monotonic time in milliseconds.
 * @return Audio and scene-reset effects for the caller to dispatch.
 */
AppActions app_update(AppState* app, const Audio* audio, const AudioSourceStatus* source, InputState input, uint32_t now);
