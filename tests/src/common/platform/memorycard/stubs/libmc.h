#pragma once

#define MC_TYPE_MC          0
#define MC_TYPE_PS2         2
#define MC_FORMATTED        1
#define MC_WAIT             0
#define sceMcResChangedCard (-1)

/**
 * @brief Substitute mcInit in this host fixture.
 */
int mcInit(int type);

/**
 * @brief Substitute mcGetInfo in this host fixture.
 */
int mcGetInfo(int port, int slot, int* type, int* free_space, int* formatted);

/**
 * @brief Substitute mcSync in this host fixture.
 */
int mcSync(int mode, int* command, int* result);
