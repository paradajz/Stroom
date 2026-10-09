#pragma once

#include <stdint.h>

/** @brief Bounded renderer yield while background artwork decoding is runnable. */
unsigned ui_background_work_budget(uint32_t elapsed_ms);
