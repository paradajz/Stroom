#pragma once

/** Failure codes for this API. */
typedef enum
{
    PS2_SCHEDULER_ERROR_THREAD_STATUS       = -1,
    PS2_SCHEDULER_ERROR_PRIORITY_CHANGE     = -2,
    PS2_SCHEDULER_ERROR_PRIORITY_RESTORE    = -3,
    PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY = -4,
} Ps2SchedulerError;

/* Smaller values preempt larger ones. Short, sleeping I/O workers must run
 * above the busy-waiting renderer; audio workers retain their higher priority. */
enum
{
    PS2_IO_WORKER_PRIORITY = 0x60,
    PS2_RENDER_PRIORITY    = 0x61
};

/**
 * @brief Place the calling application thread below SDK audio callbacks.
 *
 * Call before starting audio workers. Repeated calls preserve the original priority.
 *
 * @return 0 on success, a negative Ps2SchedulerError on failure.
 */
int platform_scheduler_open(void);

/**
 * @brief Restore the application thread priority after workers and rendering stop.
 *
 * @return 0 on success, a negative Ps2SchedulerError on failure.
 */
int platform_scheduler_close(void);

/**
 * @brief Choose one priority level below the calling thread for background work.
 * @return EE priority, or PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY if inspection fails or no lower level exists.
 */
int platform_scheduler_background_priority(void);
