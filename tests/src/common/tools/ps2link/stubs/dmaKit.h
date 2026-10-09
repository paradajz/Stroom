#pragma once

#include <stdint.h>

typedef uint16_t u16;
typedef uint32_t u32;

/**
 * @brief Substitute dmaKit_send in this host fixture.
 */
void dmaKit_send(u16 channel, void* data, u32 size);

/**
 * @brief Substitute dmaKit_send_chain in this host fixture.
 */
void dmaKit_send_chain(u16 channel, void* data, u32 size);
