#pragma once

typedef struct
{
    int init_count, max_count;
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
 * @brief Substitute iSignalSema in this host fixture.
 */
int iSignalSema(int id);

/**
 * @brief Substitute ExitHandler in this host fixture.
 */
void ExitHandler(void);
