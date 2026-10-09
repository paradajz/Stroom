#pragma once

typedef struct
{
    int init_count;
    int max_count;
} ee_sema_t;

int CreateSema(ee_sema_t* sema);
int WaitSema(int id);
int SignalSema(int id);
int DeleteSema(int id);
