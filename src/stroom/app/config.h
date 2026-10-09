#pragma once

#include "platform/network/runtime.h"
#include <stdio.h>

typedef struct
{
    NetworkConfig network;
    char          lookup_host[16];
    int           show_panels;
    int           levels;
    int           frame_rate;
    int           muted;
    int           cd_autoplay;
} AppConfig;

/**
 * @brief Build startup defaults with CD autoplay and an empty recognition address.
 * @return Default configuration; an empty lookup_host disables recognition.
 */
AppConfig app_config_defaults(void);

/**
 * @brief Apply valid key=value lines while ignoring unknown keys and invalid values.
 * @param config Configuration to update.
 * @param file Open input stream, borrowed and left open.
 */
void app_config_read(AppConfig* config, FILE* file);

/**
 * @brief Load STROOM.DAT beside the executable while retaining defaults if unavailable.
 * @param executable Executable boot path used to locate the settings file.
 * @return Configuration with accepted settings applied to defaults.
 */
AppConfig app_config_load(const char* executable);
