#pragma once

#define IOP_MUTEX_UNLOCKED 0

/**
 * @brief Substitute CreateMutex in this host fixture.
 */
int CreateMutex(int state);

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int sema);

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int sema);

/**
 * @brief Substitute iSignalSema in this host fixture.
 */
int iSignalSema(int sema);

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int sema);
