#pragma once

#define AUDIO_WORKER_STACK_BYTES 16384
#define AUDIO_WORKER_IDLE_US     1000

/* Audio workers preempt rendering and block or sleep between jobs.
 * Application-thread scheduling belongs to platform/thread/scheduler.c. */
enum
{
    AUDIO_WORKER_PRIORITY = 1
};
