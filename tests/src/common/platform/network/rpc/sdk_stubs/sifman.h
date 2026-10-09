#pragma once

struct t_SifDmaTransfer
{
    void* src;
    void* dest;
    int   size, attr;
};

int sceSifSetDma(struct t_SifDmaTransfer* transfer, int count);
int sceSifDmaStat(int id);
