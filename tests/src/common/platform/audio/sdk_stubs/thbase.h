#pragma once

#define TH_C 1

typedef struct
{
    int attr, option;
    void (*thread)(void*);
    int priority, stacksize;
} iop_thread_t;

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(iop_thread_t* thread);

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
 * @brief Substitute DelayThread in this host fixture.
 */
int DelayThread(int microseconds);

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void);
