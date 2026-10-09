#include "ui/motion.h"
#include <math.h>

#define SLIDE_MS 280.0f

float ui_motion_step(UiMotion* motion, int shown, uint32_t now)
{
    float target = shown ? 1 : 0;

    if (!motion->initialized)
    {
        motion->position    = target;
        motion->initialized = 1;
    }
    else
    {
        float step = (uint32_t)(now - motion->at) / SLIDE_MS;

        motion->position = target > motion->position ? fminf(target, motion->position + step) : fmaxf(target, motion->position - step);
    }

    motion->at = now;

    float p = motion->position;

    return p * p * (3 - 2 * p);
}
