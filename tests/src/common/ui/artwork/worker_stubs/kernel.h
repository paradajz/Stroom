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

/**
 * @brief Substitute ChangeThreadPriority in this host fixture.
 */
int ChangeThreadPriority(int id, int priority);

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void);

/**
 * @brief Substitute ReferThreadStatus in this host fixture.
 */
int ReferThreadStatus(int id, ee_thread_status_t* status);

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* config);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int id);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id);

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(ee_thread_t* config);

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* argument);

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int id);

/**
 * @brief Substitute ExitThread in this host fixture.
 */
void ExitThread(void);
