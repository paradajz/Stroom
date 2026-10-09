#pragma once

#include <tamtypes.h>

#define UNCACHED_SEG(pointer)    (pointer)
#define IS_UNCACHED_SEG(pointer) 0

typedef struct
{
    int init_count, max_count;
    u32 option, attr;
} ee_sema_t;

int  CreateSema(ee_sema_t* sema);
int  DeleteSema(int id);
int  WaitSema(int id);
int  SignalSema(int id);
void nopdelay(void);
void sceSifWriteBackDCache(void* data, int size);
