#pragma once

/** Failure codes for this API. */
typedef enum
{
    PLATFORM_ERROR_SCHEDULER_START = -1,
    PLATFORM_ERROR_NETWORK_CLOSE   = -2,
    PLATFORM_ERROR_SCHEDULER_CLOSE = -3,
} PlatformError;

/**
 * @brief Initialize RPC, startup text output and application thread scheduling.
 *
 * Call on the application thread before opening device services. Reuses the
 * resident IOP without resetting it. Device adapters are opened separately.
 *
 * @return 0 on success, a negative PlatformError on failure.
 */
int platform_open(void);

/**
 * @brief Finish residual network cleanup and restore application scheduling.
 * Call after device owners close. Retained cleanup resources require a retry.
 * @return 0 on success, a negative PlatformError on failure.
 */
int platform_close(void);
