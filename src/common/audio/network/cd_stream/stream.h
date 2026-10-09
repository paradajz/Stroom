#pragma once
#include <stdint.h>

typedef struct
{
    unsigned clients;
    int      failed;
} CdStreamStatus;

typedef enum
{
    CD_STREAM_ERROR_CLOSE = -1
} CdStreamError;

/** Application-thread lifecycle polling; failures retry independently of CD. */
CdStreamStatus cd_stream_update(int enabled);

/** CD-worker callbacks: copy only, never perform socket operations. */
void cd_stream_push(const uint8_t* samples, unsigned frames);
void cd_stream_reset(void);
void cd_stream_end(void);
void cd_stream_activity(unsigned activity);

/** Stop the worker before releasing network ownership; retry nonzero results. */
int cd_stream_close(void);
