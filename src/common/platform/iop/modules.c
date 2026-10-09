#include "util/diagnostics.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <smod.h>
#include <stdio.h>
#include <string.h>

int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    if (capacity)
    {
        error[0] = 0;
    }

    smod_mod_info_t info;

    if (smod_get_mod_by_name(module->name, &info) > 0)
    {
        return 0;
    }

    int result = 0;
    int id;

    if (module->path)
    {
        id = SifLoadModule(module->path, module->args_size, module->args);
    }
    else
    {
        /* Already-patched LOADFILE can return nonzero; loading decides success. */
        sbv_patch_enable_lmb();

        result = -1;
        id     = SifExecModuleBuffer(module->data, module->size, module->args_size, module->args, &result);
    }

    STROOM_LOG("IOP module %s: id=%d result=%d", module->name, id, result);

    if (id < 0 || result != 0)
    {
        if (capacity)
        {
            snprintf(error, capacity, "%s %s %d", module->name, id < 0 ? "LOAD" : "START", id < 0 ? id : result);
        }

        return id < 0 ? PS2_IOP_ERROR_MODULE_LOAD : PS2_IOP_ERROR_MODULE_START;
    }

    return 0;
}

int platform_iop_probe(unsigned id)
{
    SifRpcClientData_t client __attribute__((aligned(PS2_RPC_ALIGNMENT)));
    memset(&client, 0, sizeof(client));

    if (sceSifBindRpc(&client, id, 0) < 0)
    {
        return PS2_IOP_ERROR_RPC_BIND;
    }

    return client.server != NULL;
}
