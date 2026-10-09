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

int                 sceSifBindRpc(SifRpcClientData_t* client, int id, int mode);
void                sceSifInitRpc(int mode);
int                 sceSifCallRpc(SifRpcClientData_t* client, int command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument);
void                sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread);
void                sceSifRegisterRpc(SifRpcServerData_t* server, int id, void* (*handler)(int, void*, int), void* buffer, void* callback, void* callback_buffer, SifRpcDataQueue_t* queue);
void                sceSifRpcLoop(SifRpcDataQueue_t* queue);
SifRpcServerData_t* sceSifRemoveRpc(SifRpcServerData_t* server, SifRpcDataQueue_t* queue);
SifRpcDataQueue_t*  sceSifRemoveRpcQueue(SifRpcDataQueue_t* queue);
int                 sceSifSetDma(SifDmaTransfer_t* transfer, int count);
int                 sceSifDmaStat(int id);
