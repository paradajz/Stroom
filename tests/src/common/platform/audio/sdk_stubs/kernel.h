#pragma once

typedef struct
{
    int init_count, max_count, option;
} ee_sema_t;

typedef struct
{
    int attr, option;
    void (*func)(void*);
    void* stack;
    int   stack_size;
    void* gp_reg;
    int   initial_priority;
} ee_thread_t;

int  CreateSema(ee_sema_t* sema);
int  CreateThread(ee_thread_t* thread);
int  StartThread(int thread, void* argument);
int  TerminateThread(int thread);
int  DeleteThread(int thread);
int  WaitSema(int sema);
int  SignalSema(int sema);
int  DeleteSema(int sema);
int  GetThreadId(void);
void nopdelay(void);
