#include "platform/thread/worker.h"
#include "util/diagnostics.h"
#include <kernel.h>

/** @brief Report a failed SDK cleanup operation without changing its resource handle. */
static int cleanup_succeeded(const char* operation, int resource, int result)
{
    if (result >= 0)
    {
        return 1;
    }

    STROOM_LOG("worker cleanup %s id=%d failed: %d", operation, resource, result);
    (void)operation;
    (void)resource;

    return 0;
}

/**
 * @brief Release synchronization after startup failure or a confirmed join.
 * @param worker Inactive worker resources.
 */
static int release_semaphores(Ps2Worker* worker)
{
    if (worker->wake < 0 || cleanup_succeeded("DeleteSema(wake)", worker->wake, DeleteSema(worker->wake)))
    {
        worker->wake = -1;
    }

    if (worker->done < 0 || cleanup_succeeded("DeleteSema(done)", worker->done, DeleteSema(worker->done)))
    {
        worker->done = -1;
    }

    if (worker->lock < 0 || cleanup_succeeded("DeleteSema(lock)", worker->lock, DeleteSema(worker->lock)))
    {
        worker->lock = -1;
    }

    return worker->wake < 0 && worker->done < 0 && worker->lock < 0;
}

int platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    if (worker->thread >= 0 || worker->lock >= 0 || worker->done >= 0 || worker->wake >= 0)
    {
        if (error)
        {
            *error = (Ps2WorkerError){ "WORKER STILL OPEN", -1 };
        }

        return PS2_WORKER_ERROR_ALREADY_OPEN;
    }

    worker->completed = 0;

    ee_sema_t sema  = { 0 };
    sema.init_count = sema.max_count = 1;
    worker->lock                     = CreateSema(&sema);
    const char* stage                = "WORKER LOCK";
    int         failure              = PS2_WORKER_ERROR_LOCK_CREATE;
    int         code                 = worker->lock;

    if (code < 0)
    {
        goto failed;
    }

    sema.init_count = 0;
    worker->done    = CreateSema(&sema);
    stage           = "WORKER DONE";
    failure         = PS2_WORKER_ERROR_DONE_CREATE;
    code            = worker->done;

    if (code < 0)
    {
        goto failed;
    }

    if (config->notifications)
    {
        worker->wake = CreateSema(&sema);
        stage        = "WORKER WAKE";
        failure      = PS2_WORKER_ERROR_WAKE_CREATE;
        code         = worker->wake;

        if (code < 0)
        {
            goto failed;
        }
    }

    ee_thread_t thread      = { 0 };
    thread.func             = config->entry;
    thread.stack            = config->stack;
    thread.stack_size       = config->stack_bytes;
    thread.gp_reg           = &_gp;
    thread.initial_priority = config->priority;
    worker->thread          = CreateThread(&thread);
    stage                   = "THREAD CREATE";
    failure                 = PS2_WORKER_ERROR_THREAD_CREATE;
    code                    = worker->thread;

    if (code < 0)
    {
        goto failed;
    }

    worker->running = 1;
    stage           = "THREAD START";
    failure         = PS2_WORKER_ERROR_THREAD_START;
    code            = StartThread(worker->thread, config->argument);

    if (code < 0)
    {
        goto failed;
    }

    return 0;
failed:
    worker->running = 0;

    /* Creation failures are negative SDK codes, not live resource IDs. */

    if (worker->thread < 0 || cleanup_succeeded("DeleteThread(rollback)", worker->thread, DeleteThread(worker->thread)))
    {
        worker->thread = -1;

        release_semaphores(worker);
    }

    if (error)
    {
        *error = (Ps2WorkerError){ stage, code };
    }

    return failure;
}

void platform_worker_lock(const Ps2Worker* worker)
{
    WaitSema(worker->lock);
}

void platform_worker_unlock(const Ps2Worker* worker)
{
    SignalSema(worker->lock);
}

void platform_worker_wait(const Ps2Worker* worker)
{
    WaitSema(worker->wake);
}

void platform_worker_notify(const Ps2Worker* worker)
{
    SignalSema(worker->wake);
}

void platform_worker_finish(Ps2Worker* worker)
{
    worker->running = 0;

    SignalSema(worker->done);
    ExitThread();
}

int platform_worker_close(Ps2Worker* worker)
{
    worker->running = 0;

    if (worker->wake >= 0)
    {
        platform_worker_notify(worker);
    }

    if (worker->thread >= 0)
    {
        if (!worker->completed)
        {
            ee_thread_status_t status;

            if (!cleanup_succeeded("ReferThreadStatus", worker->thread, ReferThreadStatus(worker->thread, &status)))
            {
                return PS2_WORKER_ERROR_THREAD_STATUS;
            }

            /* Completion is signaled before ExitThread; retain the stack until
             * the kernel confirms that the thread has exited. */

            if (status.status != THS_DORMANT)
            {
                return 1;
            }

            worker->completed = 1;
        }

        if (!cleanup_succeeded("DeleteThread", worker->thread, DeleteThread(worker->thread)))
        {
            return PS2_WORKER_ERROR_THREAD_DELETE;
        }

        worker->thread = -1;
    }

    if (!release_semaphores(worker))
    {
        return PS2_WORKER_ERROR_SEMAPHORE_DELETE;
    }

    worker->completed = 0;

    return 0;
}
