#pragma once

/** @brief Semaphore creation parameters used by the CD worker. */
typedef struct
{
    int init_count; /**< Initial count. */
    int max_count;  /**< Maximum count. */
} ee_sema_t;

/** @brief Thread creation parameters used by the CD worker. */
typedef struct
{
    void (*func)(void*);    /**< Worker entry point. */
    void* stack;            /**< Stack storage. */
    int   stack_size;       /**< Stack bytes. */
    void* gp_reg;           /**< EE global pointer. */
    int   initial_priority; /**< Scheduling priority. */
} ee_thread_t;

extern int _gp;
int        CreateSema(ee_sema_t* sema);
int        WaitSema(int id);
int        SignalSema(int id);
int        DeleteSema(int id);
int        CreateThread(ee_thread_t* thread);
int        StartThread(int id, void* arg);
int        DeleteThread(int id);
void       ExitThread(void);

#define THS_DORMANT 0x10

typedef struct
{
    int status;
} ee_thread_status_t;

int ReferThreadStatus(int id, ee_thread_status_t* status);
