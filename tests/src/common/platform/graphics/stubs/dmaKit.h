#pragma once

enum
{
    D_CTRL_RELE_OFF,
    D_CTRL_MFD_OFF,
    D_CTRL_STS_UNSPEC,
    D_CTRL_STD_OFF,
    D_CTRL_RCYC_8,
    DMA_CHANNEL_GIF
};

/**
 * @brief Substitute dmaKit_init in this host fixture.
 */
void dmaKit_init(int a, int b, int c, int d, int e, int f);

/**
 * @brief Substitute dmaKit_chan_init in this host fixture.
 */
void dmaKit_chan_init(int channel);
