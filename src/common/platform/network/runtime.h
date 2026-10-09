#pragma once

#include <stddef.h>
#include <stdint.h>

/** Host-order static addresses; zero ip selects automatic configuration. */
typedef struct
{
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
} NetworkConfig;

/** Network startup and cleanup failures; incompatible resident modules require a console restart. */
typedef enum
{
    PLATFORM_NETWORK_ERROR_CLEANUP          = -1,
    PLATFORM_NETWORK_START_RESTART_REQUIRED = -2,
    PLATFORM_NETWORK_ERROR_MODULE_LOAD      = -3,
    PLATFORM_NETWORK_ERROR_RPC_UNAVAILABLE  = -4,
    PLATFORM_NETWORK_ERROR_INITIALIZE       = -5,
    PLATFORM_NETWORK_ERROR_INTERFACE        = -6,
    PLATFORM_NETWORK_ERROR_CONFIGURE        = -7,
    PLATFORM_NETWORK_ERROR_CONFIG_VERIFY    = -8
} PlatformNetworkError;

/**
 * @brief Set startup addressing before the first network owner opens.
 * @param config Host-order startup addresses; a zero ip selects automatic configuration.
 */
void platform_network_configure(const NetworkConfig* config);

/**
 * @brief Reuse or initialize IOP Ethernet services and connect the EE socket client.
 * Calls are reference-counted; open/close must run on the application thread.
 * Existing addresses and DHCP clients are preserved. Unconfigured interfaces use
 * configured static addresses or DHCP asynchronously. Resident modules remain loaded after failure or shutdown.
 * @param error Destination for a startup failure message; cleared on success.
 * @param capacity Size of the error destination in bytes.
 * @return 0 on success, PLATFORM_NETWORK_START_RESTART_REQUIRED for incompatible
 * resident modules, or another PlatformNetworkError. Retry partial cleanup through
 * platform_network_close(); retained resources prevent reopening.
 */
int platform_network_startup(char* error, size_t capacity);

/**
 * @brief Copy the Ethernet IPv4 address without requiring active carrier.
 * @param address Destination for dotted IPv4 text, cleared while unavailable.
 * @param capacity Destination capacity in bytes.
 */
void platform_network_address(char* address, size_t capacity);

/**
 * @brief Copy an IPv4 address only when Ethernet carrier is active.
 * @param address Destination for dotted IPv4 text, cleared while unavailable.
 * @param capacity Destination capacity in bytes.
 */
void platform_network_ready_address(char* address, size_t capacity);

/**
 * @brief Release one owner, or retry cleanup left by failed startup.
 * The last owner is retained if cleanup fails; retry before reopening.
 * @return 0 on success, a negative PlatformNetworkError on failure.
 */
int platform_network_close(void);
