#pragma once

struct t_SifDmaTransfer
{
    void* src;
    void* dest;
    int   size, attr;
};

/**
 * @brief Substitute sceSifSetDma in this host fixture.
 */
int sceSifSetDma(struct t_SifDmaTransfer* transfer, int count);

/**
 * @brief Substitute sceSifDmaStat in this host fixture.
 */
int sceSifDmaStat(int id);
