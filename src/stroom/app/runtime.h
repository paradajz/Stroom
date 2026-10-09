#pragma once

#include "app/controller.h"

/** Failure codes for this API. */
typedef enum
{
    APP_RUNTIME_ERROR_ALREADY_OPEN   = -1,
    APP_RUNTIME_ERROR_PLATFORM_START = -2,
    APP_RUNTIME_ERROR_RENDERER_START = -3,
    APP_RUNTIME_ERROR_ARTWORK_CLOSE  = -4,
    APP_RUNTIME_ERROR_LOOKUP_CLOSE   = -5,
    APP_RUNTIME_ERROR_AUDIO_CLOSE    = -6,
    APP_RUNTIME_ERROR_RENDERER_CLOSE = -7,
    APP_RUNTIME_ERROR_PAD_CLOSE      = -8,
    APP_RUNTIME_ERROR_PLATFORM_CLOSE = -9,
} AppRuntimeError;

/**
 * @brief Application-thread state; zero-initialize before its first open.
 *
 * Started flags record cleanup ownership, including failed startup attempts.
 * They clear only after the corresponding close succeeds.
 */
typedef struct
{
    Audio             audio;
    AppState          app;
    AudioSourceStatus source;
    int               platform_opened;
    int               pad_open;
    int               lookup_started;
    int               audio_started;
    int               renderer_started;
    int               artwork_started;
    int               ready;
    int               closing;
} AppRuntime;

/**
 * @brief Initialize application services in dependency order.
 *
 * Only one runtime may own the subsystem services. Rejects reopening while
 * active or while cleanup is pending, preserving its state. Call close after
 * failure, including partially completed startup.
 *
 * @param runtime Zero-initialized or successfully closed runtime.
 * @param argc Argument count.
 * @param argv Arguments; argv[0] locates startup configuration.
 * @return 0 on success, a negative AppRuntimeError on failure.
 */
int app_runtime_open(AppRuntime* runtime, int argc, char** argv);

/**
 * @brief Poll, update, dispatch and render exactly one frame.
 * @param runtime Open runtime; does nothing before startup or after close begins.
 */
void app_runtime_step(AppRuntime* runtime);

/**
 * @brief Attempt each pending cleanup stage once, in dependency order.
 *
 * Stops frame execution immediately. Closes workers before graphics and platform
 * services; completed stages are not repeated. Active workers leave cleanup
 * pending for a later attempt. Safe after failed startup and after successful close.
 *
 * @param runtime Runtime whose resources should be released.
 * @return 0 after cleanup, positive while stopping, a negative AppRuntimeError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int app_runtime_close(AppRuntime* runtime);
