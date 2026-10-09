#pragma once

#define MAX_PRIORITY 128
#define THS_READY    2
#define THS_DORMANT  16

typedef struct
{
    int init_count, max_count;
} ee_sema_t;

typedef struct
{
    int current_priority, status;
} ee_thread_status_t;

typedef struct
{
    void (*func)(void*);
    void*    stack;
    unsigned stack_size;
    void*    gp_reg;
    int      initial_priority;
} ee_thread_t;

extern void* _gp;
int          ChangeThreadPriority(int id, int priority);
int          GetThreadId(void);
int          ReferThreadStatus(int id, ee_thread_status_t* status);
int          CreateSema(ee_sema_t* config);
int          DeleteSema(int id);
int          WaitSema(int id);
int          SignalSema(int id);
int          CreateThread(ee_thread_t* config);
int          StartThread(int id, void* argument);
int          DeleteThread(int id);
void         ExitThread(void);
