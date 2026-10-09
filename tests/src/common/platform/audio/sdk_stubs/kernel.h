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

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* sema);

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(ee_thread_t* thread);

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int thread, void* argument);

/**
 * @brief Substitute TerminateThread in this host fixture.
 */
int TerminateThread(int thread);

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int thread);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int sema);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int sema);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int sema);

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void);

/**
 * @brief Substitute nopdelay in this host fixture.
 */
void nopdelay(void);
