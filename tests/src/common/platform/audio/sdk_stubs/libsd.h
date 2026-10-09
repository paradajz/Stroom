#pragma once

#define SD_PARAM_AVOLL 1
#define SD_PARAM_AVOLR 2
#define SD_PARAM_BVOLL 3
#define SD_PARAM_BVOLR 4
#define SD_PARAM_MVOLL 5
#define SD_PARAM_MVOLR 6
#define SD_TRANS_LOOP  1
#define SD_TRANS_STOP  2

/**
 * @brief Substitute sceSdInit in this host fixture.
 */
int sceSdInit(int mode);

/**
 * @brief Substitute sceSdSetParam in this host fixture.
 */
void sceSdSetParam(int param, unsigned short value);

/**
 * @brief Substitute sceSdSetTransCallback in this host fixture.
 */
void* sceSdSetTransCallback(int channel, void* callback);

/**
 * @brief Substitute sceSdBlockTrans in this host fixture.
 */
int sceSdBlockTrans(int channel, int mode, void* buffer, int bytes, void* argument);

/**
 * @brief Substitute sceSdBlockTransStatus in this host fixture.
 */
int sceSdBlockTransStatus(int channel, int mode);
