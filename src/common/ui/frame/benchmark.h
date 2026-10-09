#pragma once

#if STROOM_PRESET_BENCHMARK
#include <gsKit.h>

/**
 * @brief Borrow the renderer context for benchmark-only GPU validation.
 * @return Context after successful ui_frame_open(); valid until ui_frame_close() succeeds; do not draw while cleanup is pending.
 */
GSGLOBAL* ui_frame_benchmark_context(void);

/**
 * @brief Draw and present the benchmark failure report on the renderer thread.
 * @param lines Borrowed message lines.
 * @param count Number of lines.
 */
void ui_frame_benchmark_message(const char* const* lines, unsigned count);
#endif
