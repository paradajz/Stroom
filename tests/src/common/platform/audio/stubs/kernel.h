#pragma once

typedef struct
{
    int init_count;
    int max_count;
} ee_sema_t;

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* sema);

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
