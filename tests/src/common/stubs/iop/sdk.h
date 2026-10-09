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

/**
 * @brief Substitute smod_get_mod_by_name in this host fixture.
 */
int smod_get_mod_by_name(const char* name, smod_mod_info_t* info);

/**
 * @brief Substitute SifLoadModule in this host fixture.
 */
int SifLoadModule(const char* path, int length, const char* args);

/**
 * @brief Substitute SifExecModuleBuffer in this host fixture.
 */
int SifExecModuleBuffer(void* data, unsigned size, unsigned length, const char* args, int* result);

/**
 * @brief Substitute sceSifBindRpc in this host fixture.
 */
int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode);

/**
 * @brief Substitute sbv_patch_enable_lmb in this host fixture.
 */
int sbv_patch_enable_lmb(void);
