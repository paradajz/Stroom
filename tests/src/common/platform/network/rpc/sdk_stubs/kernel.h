#pragma once

#include <tamtypes.h>

#define UNCACHED_SEG(pointer)    (pointer)
#define IS_UNCACHED_SEG(pointer) 0

typedef struct
{
    int init_count, max_count;
    u32 option, attr;
} ee_sema_t;

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* sema);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int id);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id);

/**
 * @brief Substitute nopdelay in this host fixture.
 */
void nopdelay(void);

/**
 * @brief Substitute sceSifWriteBackDCache in this host fixture.
 */
void sceSifWriteBackDCache(void* data, int size);
