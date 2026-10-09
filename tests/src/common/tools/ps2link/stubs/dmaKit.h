#pragma once

#include <stdint.h>

typedef uint16_t u16;
typedef uint32_t u32;

void dmaKit_send(u16 channel, void* data, u32 size);
void dmaKit_send_chain(u16 channel, void* data, u32 size);
