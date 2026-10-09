#pragma once

#include <stdint.h>

/**
 * @brief Calculate the renderer yield available to runnable artwork decoding.
 * @param elapsed_ms Milliseconds spent on the current display frame.
 * @return Yield budget in milliseconds, with a minimum background slice.
 */
unsigned ui_background_work_budget(uint32_t elapsed_ms);
