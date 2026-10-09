#include "ui/presentation.h"
#include "platform/graphics/display_config.h"

/* Reserve 4 ms of the display frame for synchronization/presentation,
 * but give a runnable decoder at least 4 ms even after an expensive frame. */
#define PRESENTATION_RESERVE_MS     4u
#define BACKGROUND_MINIMUM_SLICE_MS 4u

unsigned ui_background_work_budget(uint32_t elapsed_ms)
{
    const unsigned frame_budget_ms = DISPLAY_FRAME_US / 1000u - PRESENTATION_RESERVE_MS;
    unsigned       available       = elapsed_ms < frame_budget_ms ? frame_budget_ms - elapsed_ms : 0;

    return available > BACKGROUND_MINIMUM_SLICE_MS ? available : BACKGROUND_MINIMUM_SLICE_MS;
}
