#pragma once

#include <tamtypes.h>

typedef struct
{
    void* server;
} SifRpcClientData_t;

typedef struct
{
    int unused;
} SifRpcDataQueue_t;

typedef struct
{
    int unused;
} SifRpcServerData_t;

/**
 * @brief Substitute sceSifBindRpc in this host fixture.
 */
int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode);

/**
 * @brief Substitute sceSifCallRpc in this host fixture.
 */
int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument);

/**
 * @brief Substitute sceSifSetRpcQueue in this host fixture.
 */
void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread);

/**
 * @brief Substitute sceSifRegisterRpc in this host fixture.
 */
void sceSifRegisterRpc(SifRpcServerData_t* server, unsigned id, void* (*callback)(unsigned, void*, int), void* storage, void* completion, void* argument, SifRpcDataQueue_t* queue);

/**
 * @brief Substitute sceSifRpcLoop in this host fixture.
 */
void sceSifRpcLoop(SifRpcDataQueue_t* queue);

typedef struct
{
    int unused;
} SifRpcReceiveData_t;

/**
 * @brief Substitute sceSifGetOtherData in this host fixture.
 */
int sceSifGetOtherData(SifRpcReceiveData_t* receive, void* source, void* destination, int size, int mode);
