#pragma once

#define MAX_PRIORITY 128

/**
 * @brief Minimal EE thread-status fixture for scheduler tests.
 */
typedef struct
{
    int current_priority; /**< Mock thread scheduling priority. */
} ee_thread_status_t;

/**
 * @brief Return the mock application thread ID.
 *
 * @return Configured thread ID or negative error.
 */
int GetThreadId(void);

/**
 * @brief Read the mock thread priority.
 *
 * @param thread Expected application thread ID.
 * @param status Destination status.
 * @return 0 on success; -1 for an injected failure.
 */
int ReferThreadStatus(int thread, ee_thread_status_t* status);

/**
 * @brief Record a mock thread-priority change.
 *
 * @param thread Expected application thread ID.
 * @param priority Requested priority.
 * @return 0 on success; -1 for an injected failure.
 */
int ChangeThreadPriority(int thread, int priority);
