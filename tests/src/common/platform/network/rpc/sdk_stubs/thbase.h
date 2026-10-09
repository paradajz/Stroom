#pragma once

#define TH_C 1

typedef struct
{
    int attr, option;
    void (*thread)(void*);
    int stacksize, priority;
} iop_thread_t;

int CreateThread(iop_thread_t* thread);
int StartThread(int id, void* argument);
int GetThreadId(void);
