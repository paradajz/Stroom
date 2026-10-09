#include "util/diagnostics.h"
#include "platform/input/pad.h"
#include <stdio.h>
#include <stdint.h>
#include <debug.h>
#include <string.h>
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include <unistd.h>
#include <libpad.h>

#define PAD_DMA_BYTES     256
#define PAD_DMA_ALIGNMENT 64

static unsigned char pad_buffer[PAD_DMA_BYTES] __attribute__((aligned(PAD_DMA_ALIGNMENT)));
static unsigned      previous;

/**
 * @brief Wait up to roughly one second for either supported controller RPC pair.
 *
 * Probe first because libpad initialization can wait indefinitely for missing services.
 *
 * @return 1 if available; 0 on timeout.
 */
static int pad_rpc_ready(void)
{
    for (int attempt = 0; attempt < PS2_RPC_PROBE_ATTEMPTS; ++attempt)
    {
        if ((platform_iop_probe(PS2_RPC_PAD_NEW_1) > 0 && platform_iop_probe(PS2_RPC_PAD_NEW_2) > 0) ||
            (platform_iop_probe(PS2_RPC_PAD_OLD_1) > 0 && platform_iop_probe(PS2_RPC_PAD_OLD_2) > 0))
        {
            return 1;
        }

        usleep(PS2_RPC_PROBE_DELAY_US);
    }

    scr_printf("No controller RPC service; continuing without a pad.\n");
    STROOM_LOG("controller RPC service unavailable");

    return 0;
}

int platform_pad_open(void)
{
    if (platform_iop_module(&(Ps2IopModule){ .name = "sio2man", .path = "rom0:SIO2MAN" }, NULL, 0) != 0 ||
        platform_iop_module(&(Ps2IopModule){ .name = "padman", .path = "rom0:PADMAN" }, NULL, 0) != 0)
    {
        return PS2_PAD_ERROR_MODULE_LOAD;
    }

    if (!pad_rpc_ready())
    {
        return PS2_PAD_ERROR_RPC_UNAVAILABLE;
    }

    STROOM_LOG("Binding controller RPC...");

    if (padInit(0) != 1)
    {
        return PS2_PAD_ERROR_INITIALIZE;
    }

    STROOM_LOG("Opening controller port...");

    if (!padPortOpen(0, 0, pad_buffer))
    {
        return PS2_PAD_ERROR_PORT_OPEN;
    }

    previous = 0;

    return 0;
}

/**
 * @brief Read native controller button bits from port zero.
 *
 * @param enabled Nonzero if the port was opened.
 * @param connected Destination connection flag.
 * @return Pressed PAD_* bits, or zero when unavailable.
 */
static unsigned int read_pad(int enabled, int* connected)
{
    struct padButtonStatus buttons;

    *connected = 0;

    if (!enabled)
    {
        return 0;
    }

    int state = padGetState(0, 0);

    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
    {
        return 0;
    }

    if (!padRead(0, 0, &buttons))
    {
        return 0;
    }

    *connected = 1;

    return (unsigned int)(buttons.btns ^ UINT16_MAX);
}

InputState platform_pad_read(int enabled)
{
    InputState input = { 0 };
    unsigned   pad   = read_pad(enabled, &input.connected);

    if (pad & PAD_UP)
    {
        input.held |= INPUT_UP;
    }

    if (pad & PAD_DOWN)
    {
        input.held |= INPUT_DOWN;
    }

    if (pad & PAD_LEFT)
    {
        input.held |= INPUT_LEFT;
    }

    if (pad & PAD_RIGHT)
    {
        input.held |= INPUT_RIGHT;
    }

    if (pad & PAD_CROSS)
    {
        input.held |= INPUT_CROSS;
    }

    if (pad & PAD_SQUARE)
    {
        input.held |= INPUT_SQUARE;
    }

    if (pad & PAD_TRIANGLE)
    {
        input.held |= INPUT_TRIANGLE;
    }

    if (pad & PAD_L1)
    {
        input.held |= INPUT_L1;
    }

    if (pad & PAD_R1)
    {
        input.held |= INPUT_R1;
    }

    if (pad & PAD_L2)
    {
        input.held |= INPUT_L2;
    }

    if (pad & PAD_R2)
    {
        input.held |= INPUT_R2;
    }

    if (pad & PAD_START)
    {
        input.held |= INPUT_START;
    }

    if (pad & PAD_SELECT)
    {
        input.held |= INPUT_SELECT;
    }

    input.pressed = input.held & ~previous;
    previous      = input.held;

    return input;
}

int platform_pad_close(int enabled)
{
    if (enabled)
    {
        int result = padPortClose(0, 0);

        if (result != 1)
        {
            STROOM_LOG("controller cleanup padPortClose failed: %d", result);
            return PS2_PAD_ERROR_PORT_CLOSE;
        }
    }

    return 0;
}
