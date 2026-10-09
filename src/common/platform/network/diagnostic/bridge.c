#include "platform/network/diagnostic/wire.h"
#include <loadcore.h>
#include <string.h>

#define PS2IP_DIAGNOSTIC_LIBRARY_VERSION 0x0206

/**
 * @brief Optional diagnostic-export signature.
 */
typedef void (*DiagnosticExport)(const Ps2DiagnosticRequest*, Ps2DiagnosticReply*);

/**
 * @brief Resolve the pinned SDK's appended diagnostic export without a hard dependency.
 * @return Export pointer, or null for an older resident stack.
 */
static DiagnosticExport resolve(void)
{
    iop_library_t library = { .version = PS2IP_DIAGNOSTIC_LIBRARY_VERSION, .name = "ps2ip" };
    void**        exports = QueryLibraryEntryTable(&library);

    if (!exports)
    {
        return NULL;
    }

    for (unsigned i = 0; i <= DIAGNOSTIC_IOP_EXPORT; ++i)
    {
        if (!exports[i])
        {
            return NULL;
        }
    }

    return (DiagnosticExport)exports[DIAGNOSTIC_IOP_EXPORT];
}

void platform_net_diagnostic(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply)
{
    DiagnosticExport query = resolve();

    memset(reply, 0, sizeof(*reply));

    if (query)
    {
        query(request, reply);
    }
}

void platform_net_diagnostic_read(int socket, int result, int complete)
{
    static DiagnosticExport query;
    static int              resolved;

    if (!resolved)
    {
        query    = resolve();
        resolved = 1;
    }

    if (query)
    {
        Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = complete ? DIAGNOSTIC_IOP_READ_END : DIAGNOSTIC_IOP_READ_BEGIN, .socket = socket, .result = result };

        query(&request, NULL);
    }
}
