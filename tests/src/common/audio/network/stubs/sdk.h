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
int        CreateSema(ee_sema_t* s);
int        WaitSema(int id);
int        SignalSema(int id);
int        DeleteSema(int id);
int        CreateThread(ee_thread_t* t);
int        StartThread(int id, void* arg);
int        DeleteThread(int id);
void       ExitThread(void);
int        scr_printf(const char* format, ...);
int        libcglue_ps2ip_getconfig(char* name, t_ip_info* info);
int        libcglue_ps2ip_setconfig(const t_ip_info* info);
int        ps2ip_init(void);
void       ps2ip_deinit(void);
int        _ps2sdk_ioctl(int fd, int command, void* value);

#define THS_DORMANT 0x10

typedef struct
{
    int status;
} ee_thread_status_t;

int ReferThreadStatus(int id, ee_thread_status_t* status);
