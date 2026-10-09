#pragma once

#define TH_C 1

typedef struct
{
    int attr, option;
    void (*thread)(void*);
    int priority, stacksize;
} iop_thread_t;

int CreateThread(iop_thread_t* thread);
int StartThread(int thread, void* argument);
int TerminateThread(int thread);
int DeleteThread(int thread);
int DelayThread(int microseconds);
int GetThreadId(void);
