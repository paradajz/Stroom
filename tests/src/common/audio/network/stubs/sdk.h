#pragma once

#include "iop/sdk.h"
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

/**
 * @brief Minimal semaphore fixture for the network worker.
 */
typedef struct
{
    int init_count; /**< Initial semaphore count. */
    int max_count;  /**< Maximum semaphore count. */
} ee_sema_t;

/**
 * @brief Thread creation parameters consumed by the network fixture.
 */
typedef struct
{
    void (*func)(void*);    /**< Worker entry point. */
    void* stack;            /**< Worker stack storage. */
    int   stack_size;       /**< Stack size in bytes. */
    void* gp_reg;           /**< EE global pointer. */
    int   initial_priority; /**< Requested worker priority. */
} ee_thread_t;

/**
 * @brief Mock interface configuration.
 */
typedef struct
{
    char           netif_name[4]; /**< Interface name, or empty when absent. */
    unsigned       dhcp_enabled;  /**< DHCP client enabled flag. */
    struct in_addr netmask;
    struct in_addr gw;
    struct in_addr ipaddr; /**< Network-order IPv4 address. */
} t_ip_info;

extern int _gp;

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* s);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int id);

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(ee_thread_t* t);

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* arg);

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int id);

/**
 * @brief Substitute ExitThread in this host fixture.
 */
void ExitThread(void);

/**
 * @brief Substitute scr_printf in this host fixture.
 */
int scr_printf(const char* format, ...);

/**
 * @brief Substitute libcglue_ps2ip_getconfig in this host fixture.
 */
int libcglue_ps2ip_getconfig(char* name, t_ip_info* info);

/**
 * @brief Substitute libcglue_ps2ip_setconfig in this host fixture.
 */
int libcglue_ps2ip_setconfig(const t_ip_info* info);

/**
 * @brief Substitute ps2ip_init in this host fixture.
 */
int ps2ip_init(void);

/**
 * @brief Substitute ps2ip_deinit in this host fixture.
 */
void ps2ip_deinit(void);

/**
 * @brief Substitute _ps2sdk_ioctl in this host fixture.
 */
int _ps2sdk_ioctl(int fd, int command, void* value);

#define THS_DORMANT 0x10

typedef struct
{
    int status;
} ee_thread_status_t;

/**
 * @brief Substitute ReferThreadStatus in this host fixture.
 */
int ReferThreadStatus(int id, ee_thread_status_t* status);
