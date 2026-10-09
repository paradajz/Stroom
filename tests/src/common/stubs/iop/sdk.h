#pragma once

/**
 * @brief Placeholder module query result.
 */
typedef struct
{
    int unused; /**< Unused fixture field. */
} smod_mod_info_t;

/**
 * @brief RPC binding result.
 */
typedef struct
{
    void* server; /**< Bound server, or NULL. */
} SifRpcClientData_t;

int smod_get_mod_by_name(const char* name, smod_mod_info_t* info);
int SifLoadModule(const char* path, int length, const char* args);
int SifExecModuleBuffer(void* data, unsigned size, unsigned length, const char* args, int* result);
int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode);
int sbv_patch_enable_lmb(void);
