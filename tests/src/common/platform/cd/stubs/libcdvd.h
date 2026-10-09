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
 * @param changed Receives zero (no tray change).
 * @return 1 (success).
 */
int sceCdTrayReq(int mode, u32* changed);

/**
 * @brief Read disc type.
 * @return Audio CD type.
 */
int sceCdGetDiskType(void);

/**
 * @brief Read mock TOC.
 * @param bytes Destination.
 * @return 0 (failure) in this fixture.
 */
int sceCdGetToc(void* bytes);

/**
 * @brief Read completion error.
 * @return Configured error code.
 */
int sceCdGetError(void);

/**
 * @brief Count a cancellation request.
 * @return 1 after recording the cancellation request.
 */
int sceCdBreak(void);

/**
 * @brief Poll read completion.
 * @param mode Must be nonblocking.
 * @return Nonzero while the mock read is pending; zero when complete.
 */
int sceCdSync(int mode);

/**
 * @brief Start mock DMA.
 * @param start First sector.
 * @param count Sector count.
 * @param bytes DMA destination.
 * @param mode Read mode.
 * @return 1 (success).
 */
int sceCdReadCDDA(int start, int count, void* bytes, sceCdRMode* mode);
