#pragma once

#include <stddef.h>

typedef struct
{
    int Mode, Width, Height, Interlace, Field, PSM, DoubleBuffering, ZBuffering, FirstFrame, ActiveBuffer, PrimContext;
} GSGLOBAL;

enum
{
    GS_MODE_DTV_480P,
    GS_NONINTERLACED,
    GS_FRAME,
    GS_PSM_CT32,
    GS_SETTING_ON,
    GS_SETTING_OFF,
    GS_ONESHOT
};

GSGLOBAL* gsKit_init_global(void);
void      gsKit_init_screen(GSGLOBAL* gs);
void      gsKit_mode_switch(GSGLOBAL* gs, int mode);
int       gsKit_add_vsync_handler(int (*handler)(int));
void      gsKit_vsync_wait(void);
void      gsKit_sync_flip(GSGLOBAL* gs);
void      gsKit_display_buffer(GSGLOBAL* gs);
void      gsKit_setactive(GSGLOBAL* gs);
void      gsKit_queue_exec(GSGLOBAL* gs);
void      gsKit_finish(void);
void      gsKit_remove_vsync_handler(int handler);
void      gsKit_deinit_global(GSGLOBAL* gs);
