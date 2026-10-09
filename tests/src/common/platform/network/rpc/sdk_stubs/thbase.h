#pragma once

#define TH_C 1

typedef struct
{
    int attr, option;
    void (*thread)(void*);
    int stacksize, priority;
} iop_thread_t;

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(iop_thread_t* thread);

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* argument);

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void);
