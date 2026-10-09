#pragma once

#include <stdint.h>

typedef uint32_t u32;

enum
{
    SCECdINoD,
    SCECdTrayCheck,
    SCECdCDDA,
    SCECdDETCT,
    SCECdDETCTDVDD,
    SCECdNODISC,
    SCECdErNO,
    SCECdSpinX2,
    SCECdSecS2352
};

/**
 * @brief CD read mode fixture matching the adapter's SDK usage.
 */
typedef struct
{
    unsigned char retries; /**< Retry count. */
    unsigned char speed;   /**< Spindle speed. */
    unsigned char sector;  /**< Sector format. */
    unsigned char pad;     /**< Reserved byte. */
} sceCdRMode;

/**
 * @brief Initialize mock drive.
 * @param mode Initialization mode.
 * @return 1 (success).
 */
int sceCdInit(int mode);

/**
 * @brief Read mock tray state.
 * @param mode Query mode.
 * @param changed Receives the fixture's tray-change flag.
 * @return 1 (success).
 */
int sceCdTrayReq(int mode, u32* changed);

/**
 * @brief Read disc type.
 * @return Configured fixture disc type.
 */
int sceCdGetDiskType(void);

/**
 * @brief Read mock TOC.
 * @param bytes Destination.
 * @return Configured fixture result.
 */
int sceCdGetToc(void* bytes);

/**
 * @brief Read completion error.
 * @return Configured error code.
 */
int sceCdGetError(void);

/**
 * @brief Request cancellation of the fixture's pending command.
 * @return One after accepting the cancellation request.
 */
int sceCdBreak(void);

/** @brief Start an asynchronous spindle stop. */
int sceCdStop(void);

/** @brief Start an asynchronous spindle resume. */
int sceCdStandby(void);

/**
 * @brief Poll fixture read or spindle-command completion.
 * @param mode Must be nonblocking.
 * @return Nonzero while the command is pending; zero when complete.
 */
int sceCdSync(int mode);

/**
 * @brief Start mock DMA.
 * @param start First sector.
 * @param count Sector count.
 * @param bytes DMA destination.
 * @param mode Read mode.
 * @return Nonzero when the read is accepted; zero for an injected rejection.
 */
int sceCdReadCDDA(int start, int count, void* bytes, sceCdRMode* mode);
