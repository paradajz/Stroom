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

int  sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode);
int  sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument);
void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread);
void sceSifRegisterRpc(SifRpcServerData_t* server, unsigned id, void* (*callback)(unsigned, void*, int), void* storage, void* completion, void* argument, SifRpcDataQueue_t* queue);
void sceSifRpcLoop(SifRpcDataQueue_t* queue);

typedef struct
{
    int unused;
} SifRpcReceiveData_t;

int sceSifGetOtherData(SifRpcReceiveData_t* receive, void* source, void* destination, int size, int mode);
