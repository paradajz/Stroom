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

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* sema);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int id);

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(ee_thread_t* thread);

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* arg);

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int id);

/**
 * @brief Substitute ExitThread in this host fixture.
 */
void ExitThread(void);

#define THS_DORMANT 0x10

typedef struct
{
    int status;
} ee_thread_status_t;

/**
 * @brief Substitute ReferThreadStatus in this host fixture.
 */
int ReferThreadStatus(int id, ee_thread_status_t* status);
