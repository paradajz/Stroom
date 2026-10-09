#include "platform/graphics/display.h"
#include "util/diagnostics.h"
#include "platform/graphics/display_config.h"
#include <dmaKit.h>
#include <kernel.h>
#include <stdint.h>

static GSGLOBAL*         context;
static volatile uint32_t refresh_count;
static uint32_t          presented_refresh;
static int               have_presented;
static int               refresh_sema    = -1;
static int               refresh_handler = -1;

static int count_refresh(int cause)
{
    (void)cause;
    ++refresh_count;
    iSignalSema(refresh_sema);
    ExitHandler();

    return 0;
}

GSGLOBAL* platform_display_open(void)
{
    if (context)
    {
        return NULL;
    }

    GSGLOBAL* gs = context = gsKit_init_global();

    if (!gs)
    {
        return NULL;
    }

    gs->Mode            = DISPLAY_GS_MODE;
    gs->Width           = DISPLAY_WIDTH;
    gs->Height          = DISPLAY_HEIGHT;
    gs->Interlace       = DISPLAY_INTERLACE;
    gs->Field           = DISPLAY_FIELD_MODE;
    gs->PSM             = GS_PSM_CT32;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->ZBuffering      = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    ee_sema_t sema = { 0 };

    sema.max_count = 1;
    refresh_sema   = CreateSema(&sema);

    if (refresh_sema < 0)
    {
        platform_display_close(NULL);
        return NULL;
    }

    refresh_count = presented_refresh = 0;
    have_presented                    = 0;

    refresh_handler = gsKit_add_vsync_handler(count_refresh);

    if (refresh_handler < 0)
    {
        platform_display_close(NULL);

        return NULL;
    }

    return gs;
}

void platform_display_submit(GSGLOBAL* gs)
{
    gsKit_queue_exec(gs);
    /* Queue execution waits for the previous frame. Finish this frame's
     * feedback and presentation passes before showing its buffer. */
    gsKit_finish();
}

int platform_display_close(GSGLOBAL* gs)
{
    if (!context)
    {
        return 0;
    }

    if (gs && gs != context)
    {
        return PS2_DISPLAY_ERROR_INVALID_CONTEXT;
    }

    if (refresh_handler >= 0)
    {
        gsKit_remove_vsync_handler(refresh_handler);

        refresh_handler = -1;
    }

    if (refresh_sema >= 0)
    {
        int result = DeleteSema(refresh_sema);

        if (result < 0)
        {
            STROOM_LOG("display cleanup DeleteSema id=%d failed: %d", refresh_sema, result);
            return PS2_DISPLAY_ERROR_SEMAPHORE_DELETE;
        }

        refresh_sema = -1;
    }

    gsKit_deinit_global(context);

    context      = NULL;
    refresh_sema = refresh_handler = -1;

    return 0;
}

void platform_display_present(GSGLOBAL* gs, unsigned refresh_interval)
{
    if (!gs->FirstFrame)
    {
        uint32_t start    = refresh_count;
        unsigned interval = refresh_interval ? refresh_interval : 1u;
        uint32_t elapsed  = start - presented_refresh;
        unsigned wait     = have_presented && elapsed < interval ? interval - elapsed : 1u;

        /* Always present on a future refresh. The counter, not semaphore tokens,
         * determines the deadline: an old token cannot satisfy a new wait, and
         * a refresh between this check and WaitSema is retained by the semaphore.
         * Unsigned differences remain valid across counter wraparound. */

        while ((uint32_t)(refresh_count - start) < wait)
        {
            WaitSema(refresh_sema);
        }

        if (gs->DoubleBuffering == GS_SETTING_ON)
        {
            gsKit_display_buffer(gs);

            gs->ActiveBuffer ^= 1;
        }

        presented_refresh = refresh_count;
        have_presented    = 1;
    }

    /* Match sync_flip's buffer handling without its second, polling wait.
     * switch_context is not equivalent: it also changes PrimContext. */
    gsKit_setactive(gs);
}
