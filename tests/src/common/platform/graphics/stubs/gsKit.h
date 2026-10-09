#pragma once

#include <stddef.h>

typedef struct
{
    int Mode, Width, Height, Interlace, Field, PSM, DoubleBuffering, ZBuffering, FirstFrame, ActiveBuffer, PrimContext;
} GSGLOBAL;

enum
{
    GS_MODE_DTV_480P,
    GS_MODE_NTSC,
    GS_NONINTERLACED,
    GS_INTERLACED,
    GS_FIELD,
    GS_FRAME,
    GS_PSM_CT32,
    GS_SETTING_ON,
    GS_SETTING_OFF,
    GS_ONESHOT
};

/**
 * @brief Substitute gsKit_init_global in this host fixture.
 */
GSGLOBAL* gsKit_init_global(void);

/**
 * @brief Substitute gsKit_init_screen in this host fixture.
 */
void gsKit_init_screen(GSGLOBAL* gs);

/**
 * @brief Substitute gsKit_mode_switch in this host fixture.
 */
void gsKit_mode_switch(GSGLOBAL* gs, int mode);

/**
 * @brief Substitute gsKit_add_vsync_handler in this host fixture.
 */
int gsKit_add_vsync_handler(int (*handler)(int));

/**
 * @brief Substitute gsKit_vsync_wait in this host fixture.
 */
void gsKit_vsync_wait(void);

/**
 * @brief Substitute gsKit_sync_flip in this host fixture.
 */
void gsKit_sync_flip(GSGLOBAL* gs);

/**
 * @brief Substitute gsKit_display_buffer in this host fixture.
 */
void gsKit_display_buffer(GSGLOBAL* gs);

/**
 * @brief Substitute gsKit_setactive in this host fixture.
 */
void gsKit_setactive(GSGLOBAL* gs);

/**
 * @brief Substitute gsKit_queue_exec in this host fixture.
 */
void gsKit_queue_exec(GSGLOBAL* gs);

/**
 * @brief Substitute gsKit_finish in this host fixture.
 */
void gsKit_finish(void);

/**
 * @brief Substitute gsKit_remove_vsync_handler in this host fixture.
 */
void gsKit_remove_vsync_handler(int handler);

/**
 * @brief Substitute gsKit_deinit_global in this host fixture.
 */
void gsKit_deinit_global(GSGLOBAL* gs);
