#pragma once

#define SD_PARAM_AVOLL 1
#define SD_PARAM_AVOLR 2
#define SD_PARAM_BVOLL 3
#define SD_PARAM_BVOLR 4
#define SD_PARAM_MVOLL 5
#define SD_PARAM_MVOLR 6
#define SD_TRANS_LOOP  1
#define SD_TRANS_STOP  2

int   sceSdInit(int mode);
void  sceSdSetParam(int param, unsigned short value);
void* sceSdSetTransCallback(int channel, void* callback);
int   sceSdBlockTrans(int channel, int mode, void* buffer, int bytes, void* argument);
int   sceSdBlockTransStatus(int channel, int mode);
