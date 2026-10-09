#pragma once

#include "platform/cd/drive.h"

/** Playback-owned snapshot; hardware and DMA remain on the reader worker. */
typedef struct
{
    Ps2CdDrive state;
} Ps2CdReader;

/* The adapter owns one drive, matching the SDK's process-wide CDVD client.
 * Startup initializes services before audio starts. All subsequent device
 * operations, including media probes, run on the reader worker. */
int platform_cd_reader_open(Ps2CdReader* reader, const Ps2CdDriveRuntime* runtime);
int platform_cd_reader_media(Ps2CdMedia* destination);
/* Positive means pending; returned TOC bytes survive until the next request. */
int platform_cd_reader_toc(Ps2CdToc* toc);
int platform_cd_reader_idle(const Ps2CdReader* reader);
int platform_cd_reader_failed(const Ps2CdReader* reader);
/* Reset invalidates ready sectors immediately and queues cancellation/seek. */
int platform_cd_reader_reset(Ps2CdReader* reader, int start);
int platform_cd_reader_poll(Ps2CdReader* reader, uint32_t now);
/* Borrowed bytes remain valid until consumption or a playback reset. */
const uint8_t* platform_cd_reader_sector(Ps2CdReader* reader);
unsigned       platform_cd_reader_buffered(const Ps2CdReader* reader);
void           platform_cd_reader_consume(Ps2CdReader* reader);
int            platform_cd_reader_prefetch(Ps2CdReader* reader, int end);
int            platform_cd_reader_waiting(const Ps2CdReader* reader);
/* Stop issuing new reads after an output or recovery error. */
void platform_cd_reader_suspend(Ps2CdReader* reader);
/* Cooperative, nonblocking join; retain the adapter until this returns zero. */
int platform_cd_reader_close(Ps2CdReader* reader);
