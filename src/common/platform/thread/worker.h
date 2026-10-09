#pragma once

/** Failure codes for this API. */
typedef enum
{
    PS2_WORKER_ERROR_ALREADY_OPEN     = -1,
    PS2_WORKER_ERROR_LOCK_CREATE      = -2,
    PS2_WORKER_ERROR_DONE_CREATE      = -3,
    PS2_WORKER_ERROR_WAKE_CREATE      = -4,
    PS2_WORKER_ERROR_THREAD_CREATE    = -5,
    PS2_WORKER_ERROR_THREAD_START     = -6,
    PS2_WORKER_ERROR_THREAD_STATUS    = -7,
    PS2_WORKER_ERROR_THREAD_DELETE    = -8,
    PS2_WORKER_ERROR_SEMAPHORE_DELETE = -9,
} Ps2WorkerResult;

/** @brief Shared EE worker resources; initialize with PS2_WORKER_INITIALIZER. */
typedef struct
{
    int          lock;      /**< Binary mailbox semaphore, or -1. */
    int          done;      /**< Completion semaphore, or -1. */
    int          thread;    /**< Created thread identifier, or -1. */
    int          wake;      /**< Optional coalescing notification semaphore, or -1. */
    int          completed; /**< Thread exit confirmed; deletion may be retried. */
    volatile int running;   /**< Cooperative lifetime flag read by the worker. */
} Ps2Worker;

#define PS2_WORKER_INITIALIZER { .lock = -1, .done = -1, .thread = -1, .wake = -1 }

/** @brief Caller-owned worker entry, stack and scheduling configuration. */
typedef struct
{
    void (*entry)(void*); /**< Entry must finish with platform_worker_finish(). */
    void* argument;       /**< Entry argument. */
    void* stack;          /**< Borrowed 16-byte-aligned storage, retained until close succeeds. */
    int   stack_bytes;    /**< Stack size in bytes. */
    int   priority;       /**< EE scheduling priority chosen by the subsystem. */
    int   notifications;  /**< Nonzero to create a wake semaphore for an event-driven loop. */
} Ps2WorkerConfig;

/** @brief SDK startup failure retained for subsystem diagnostics. */
typedef struct
{
    const char* stage; /**< Static operation name. */
    int         code;  /**< SDK return code. */
} Ps2WorkerError;

/**
 * @brief Create synchronization and start an inactive worker; clean up failed startup.
 * @param worker Initialized inactive resources.
 * @param config Entry, argument, stack and priority; descriptor is not retained.
 * @param error Optional startup failure destination.
 * @return 0 on success, a negative Ps2WorkerResult on failure. Allocated resources reject reopening
 * without being modified, including resources retained after failed rollback or close.
 * Retry close if startup rollback cannot delete a resource.
 */
int platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error);

/**
 * @brief Acquire the worker mailbox lock.
 * @param worker Open worker resources.
 */
void platform_worker_lock(const Ps2Worker* worker);

/**
 * @brief Release the worker mailbox lock.
 * @param worker Open worker resources.
 */
void platform_worker_unlock(const Ps2Worker* worker);

/**
 * @brief Wait for a coalesced work or stop notification on the worker thread.
 * @param worker Open worker configured with notifications.
 */
void platform_worker_wait(const Ps2Worker* worker);

/**
 * @brief Notify an event-driven worker; repeated notifications coalesce.
 * @param worker Open worker configured with notifications.
 */
void platform_worker_notify(const Ps2Worker* worker);

/**
 * @brief Signal completion and exit after subsystem cleanup; call on the worker thread.
 * @param worker Open worker resources, retained until join completes.
 */
void platform_worker_finish(Ps2Worker* worker);

/**
 * @brief Request cooperative stop and attempt cleanup without waiting for thread exit.
 * @param worker Resources to close; repeated calls after success are harmless.
 * @return 0 after cleanup, positive while the thread is active, negative on SDK failure.
 * Nonzero results retain resources; retry close before reusing the stack.
 * Call outside the worker thread with no mailbox lock held. Each attempt checks
 * thread status at most once; it does not forcibly terminate device operations.
 */
int platform_worker_close(Ps2Worker* worker);
