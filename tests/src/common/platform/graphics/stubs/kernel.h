#pragma once

typedef struct
{
    int init_count, max_count;
} ee_sema_t;

int  CreateSema(ee_sema_t* sema);
int  DeleteSema(int id);
int  WaitSema(int id);
int  iSignalSema(int id);
void ExitHandler(void);
