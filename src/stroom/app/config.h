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

/** Startup defaults; an empty lookup address disables recognition. */
AppConfig app_config_defaults(void);

/** Apply valid key=value lines. Unknown keys and invalid values are ignored. */
void app_config_read(AppConfig* config, FILE* file);

/** Read STROOM.DAT beside the executable, retaining defaults if unavailable. */
AppConfig app_config_load(const char* executable);
