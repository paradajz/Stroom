#include "platform/memorycard/card.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include <libmc.h>
#include <string.h>
#include <unistd.h>

int platform_memorycard_open(const char* executable)
{
    if (strncmp(executable, "mc0:", 4) != 0 && strncmp(executable, "mc1:", 4) != 0)
    {
        return 0;
    }

    static const Ps2IopModule modules[] = {
        { .name = "sio2man", .path = "rom0:SIO2MAN" },
        { .name = "mcman", .path = "rom0:MCMAN" },
        { .name = "mcserv", .path = "rom0:MCSERV" },
    };

    for (unsigned i = 0; i < sizeof(modules) / sizeof(*modules); ++i)
    {
        if (platform_iop_module(&modules[i], NULL, 0) != 0)
        {
            return PS2_MEMORYCARD_ERROR_MODULE_LOAD;
        }
    }

    int ready = 0;

    for (int attempt = 0; attempt < PS2_RPC_PROBE_ATTEMPTS; ++attempt)
    {
        if (platform_iop_probe(PS2_RPC_MEMORYCARD) > 0)
        {
            ready = 1;

            break;
        }

        usleep(PS2_RPC_PROBE_DELAY_US);
    }

    if (!ready)
    {
        return PS2_MEMORYCARD_ERROR_RPC_UNAVAILABLE;
    }

    int initialized = mcInit(MC_TYPE_MC);

    // libmc returns -1 when its RPC client is already initialized by this ELF.

    if (initialized < -1)
    {
        return PS2_MEMORYCARD_ERROR_INITIALIZE;
    }

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        int type = 0, free_space = 0, formatted = 0, result = 0;

        if (mcGetInfo(executable[2] - '0', 0, &type, &free_space, &formatted) < 0 ||
            mcSync(MC_WAIT, NULL, &result) != 1)
        {
            return PS2_MEMORYCARD_ERROR_IO;
        }

        if (result == sceMcResChangedCard)
        {
            continue;
        }

        return result == 0 && type == MC_TYPE_PS2 && formatted == MC_FORMATTED ? 0 : PS2_MEMORYCARD_ERROR_UNAVAILABLE;
    }

    return PS2_MEMORYCARD_ERROR_UNAVAILABLE;
}
