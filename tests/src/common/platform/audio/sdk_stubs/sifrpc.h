#pragma once

typedef struct t_SifRpcClientData
{
    void* server;
} SifRpcClientData_t;

typedef struct t_SifRpcDataQueue
{
    int unused;
} SifRpcDataQueue_t;

typedef struct t_SifRpcServerData
{
    int unused;
} SifRpcServerData_t;

typedef struct
{
    void* src;
    void* dest;
    int   size, attr;
} SifDmaTransfer_t;

#define SIF_RPC_M_NOWAIT 1

/**
 * @brief Substitute sceSifBindRpc in this host fixture.
 */
int sceSifBindRpc(SifRpcClientData_t* client, int id, int mode);

/**
 * @brief Substitute sceSifInitRpc in this host fixture.
 */
void sceSifInitRpc(int mode);

/**
 * @brief Substitute sceSifCallRpc in this host fixture.
 */
int sceSifCallRpc(SifRpcClientData_t* client, int command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument);

/**
 * @brief Substitute sceSifSetRpcQueue in this host fixture.
 */
void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread);

/**
 * @brief Substitute sceSifRegisterRpc in this host fixture.
 */
void sceSifRegisterRpc(SifRpcServerData_t* server, int id, void* (*handler)(int, void*, int), void* buffer, void* callback, void* callback_buffer, SifRpcDataQueue_t* queue);

/**
 * @brief Substitute sceSifRpcLoop in this host fixture.
 */
void sceSifRpcLoop(SifRpcDataQueue_t* queue);

/**
 * @brief Substitute sceSifRemoveRpc in this host fixture.
 */
SifRpcServerData_t* sceSifRemoveRpc(SifRpcServerData_t* server, SifRpcDataQueue_t* queue);

/**
 * @brief Substitute sceSifRemoveRpcQueue in this host fixture.
 */
SifRpcDataQueue_t* sceSifRemoveRpcQueue(SifRpcDataQueue_t* queue);

/**
 * @brief Substitute sceSifSetDma in this host fixture.
 */
int sceSifSetDma(SifDmaTransfer_t* transfer, int count);

/**
 * @brief Substitute sceSifDmaStat in this host fixture.
 */
int sceSifDmaStat(int id);
