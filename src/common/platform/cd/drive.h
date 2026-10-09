#pragma once

#include "contracts/cd.h"
#include <stdint.h>
#include <stddef.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_CD_DRIVE_ERROR_RESTART_REQUIRED = -1,
    PS2_CD_DRIVE_ERROR_CDVDMAN_LOAD     = -2,
    PS2_CD_DRIVE_ERROR_CDVDFSV_LOAD     = -3,
    PS2_CD_DRIVE_ERROR_RPC_UNAVAILABLE  = -4,
    PS2_CD_DRIVE_ERROR_INITIALIZE       = -5,
    PS2_CD_DRIVE_ERROR_TOC_READ         = -6,
    PS2_CD_DRIVE_ERROR_READ             = -7,
    PS2_CD_DRIVE_ERROR_READ_START       = -8,
    PS2_CD_DRIVE_ERROR_WORKER_START     = -9,
} Ps2CdDriveError;

/** @brief Startup callbacks and diagnostic storage borrowed for platform_cd_drive_open(). */
typedef struct
{
    const volatile int* running;                       /**< Worker lifetime flag checked during startup. */
    void*               context;                       /**< Caller context passed to the progress callback. */
    void (*stage)(void* context, const char* message); /**< Synchronous startup progress sink. */
    char*  error;                                      /**< Caller-owned diagnostic buffer. */
    size_t capacity;                                   /**< Diagnostic buffer capacity. */
} Ps2CdDriveRuntime;

/** @brief Raw SDK track table; audio policy validates and decodes these bytes. */
typedef struct
{
    const uint8_t* data;  /**< Borrowed DMA storage, valid until the next TOC read. */
    size_t         size;  /**< Available bytes in data. */
    int            error; /**< SDK drive error code. */
} Ps2CdToc;

/**
 * @brief Disc identity returned by a hardware probe.
 */
typedef struct
{
    int changed;  /**< Tray reports a disc change. */
    int type;     /**< Raw drive type for status messages. */
    int audio;    /**< Recognized audio CD. */
    int checking; /**< Drive is identifying the disc. */
    int absent;   /**< Drive reports no disc. */
} Ps2CdMedia;

/* Playback reserve duration; sizes and recovery timing derive from this value. */
#define PS2_CD_RESERVE_SECONDS 10
#define PS2_CD_RESERVE_MS      (PS2_CD_RESERVE_SECONDS * 1000u)
#define PS2_CD_RESERVE_SECTORS (PS2_CD_RESERVE_SECONDS * CD_SECTORS_PER_SECOND)

/* Start playback with a short reserve; continue filling the ring while playing. */
#define PS2_CD_PREFILL_MS      500u
#define PS2_CD_PREFILL_SECTORS ((PS2_CD_PREFILL_MS * CD_SECTORS_PER_SECOND + 999u) / 1000u)

/**
 * @brief Worker-owned read-ahead state; DMA banks and the PCM reserve stay in drive.c.
 */
typedef struct
{
    int      pending;    /**< Bank receiving DMA, or -1. */
    int      counts[2];  /**< Sectors reserved by an outstanding DMA read. */
    int      bank;       /**< Bank selected for the next DMA read. */
    int      cursor;     /**< Next absolute sector to request. */
    unsigned buffered;   /**< Completed sectors in the playback reserve. */
    unsigned read_index; /**< Next reserve sector to consume. */
    unsigned completed;  /**< Successful read completions, including across retries. */
    int      chunked;    /**< Playback started or reserve filled; refill in whole chunks. */
    uint32_t read_at;    /**< Pending read start timestamp. */
    uint32_t cancel_at;  /**< Cancellation request timestamp. */
    int      cancelling; /**< Nonzero after requesting cancellation once. */
    int      failed;     /**< Latched failure; pending DMA storage must not be reused. */
} Ps2CdDrive;

/**
 * @brief Initialize CDVD without waiting for media.
 * @param d Drive state to initialize.
 * @param runtime Worker lifetime, progress sink and diagnostic destination; not retained.
 * @return 0 on success, a negative Ps2CdDriveError on failure.
 */
int platform_cd_drive_open(Ps2CdDrive* d, const Ps2CdDriveRuntime* runtime);

/**
 * @brief Probe tray and disc identity.
 * @return Current media identity.
 */
Ps2CdMedia platform_cd_drive_media(void);

/**
 * @brief Read the raw track table without a DiskReady gate or audio validation.
 * @param toc Destination raw table and drive error; populated even on failure.
 * @return 0 on success, a negative Ps2CdDriveError on failure.
 */
int platform_cd_drive_toc(Ps2CdToc* toc);

/**
 * @brief Check whether no DMA read is pending.
 * @param d Drive state.
 * @return Nonzero when idle.
 */
int platform_cd_drive_idle(const Ps2CdDrive* d);

/**
 * @brief Check whether cancellation failed and reads are disabled until restart.
 * @param d Drive state.
 * @return Nonzero if unresponsive.
 */
int platform_cd_drive_failed(const Ps2CdDrive* d);

/**
 * @brief Cancel pending reads and discard buffers at a seek.
 * @param d Drive state.
 * @param position Next absolute sector to read.
 * @return 0 on success, a negative Ps2CdDriveError on failure.
 */
int platform_cd_drive_reset(Ps2CdDrive* d, int position);

/**
 * @brief Cancel reads after five seconds; fail permanently if cancellation takes another second.
 * @param d Drive state.
 * @param now Monotonic milliseconds.
 * @return 0 when idle or a read completes, positive while pending, a negative Ps2CdDriveError on failure.
 * Use platform_cd_drive_failed() to distinguish an unresponsive drive from a read error.
 * Recoverable failures retain completed sectors and rewind the failed range for retry.
 */
int platform_cd_drive_poll(Ps2CdDrive* d, uint32_t now);

/**
 * @brief Select the next completed sector from the cached read-ahead reserve.
 * @param d Drive state.
 * @return Borrowed sector bytes, or NULL while unavailable; valid until reset or consumption.
 */
const uint8_t* platform_cd_drive_sector(Ps2CdDrive* d);

/** Reader-worker access to a completed sector without consuming it. */
const uint8_t* platform_cd_drive_peek(const Ps2CdDrive* d, unsigned offset);

/** @brief Number of completed sectors available without another drive read. */
unsigned platform_cd_drive_buffered(const Ps2CdDrive* d);

/**
 * @brief Consume the sector previously returned by platform_cd_drive_sector.
 * @param d Drive state.
 */
void platform_cd_drive_consume(Ps2CdDrive* d);

/**
 * @brief Refill the configured reserve in small DMA reads at fixed 2x speed.
 * @param d Drive state.
 * @param end Exclusive sector boundary.
 * @return 0 on success, a negative Ps2CdDriveError on failure.
 */
int platform_cd_drive_prefetch(Ps2CdDrive* d, int end);

/**
 * @brief Report whether the completed-sector reserve is empty or quarantined.
 * @param d Drive state.
 * @return Nonzero when output must wait.
 */
int platform_cd_drive_waiting(const Ps2CdDrive* d);

/**
 * @brief Cancel outstanding reads with a one-second deadline; retain DMA storage on failure.
 * @param d Drive state.
 */
void platform_cd_drive_close(Ps2CdDrive* d);
