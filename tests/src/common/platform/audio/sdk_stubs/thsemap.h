#pragma once

#define IOP_MUTEX_UNLOCKED 0

int CreateMutex(int state);
int WaitSema(int sema);
int SignalSema(int sema);
int iSignalSema(int sema);
int DeleteSema(int sema);
