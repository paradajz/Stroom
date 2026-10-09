#include "ui/shared/level_meter.h"
#include <math.h>

#define AMPLITUDE_DB_FACTOR   20
#define METER_RMS_ATTACK_MS   40.0f
#define METER_RMS_RELEASE_MS  180.0f
#define METER_RANGE_DB        60.0f
#define METER_AMPLITUDE_FLOOR 0.001f

static float    meter_rms[2];
static uint32_t meter_at;
static int      meter_ready;

/**
 * @brief Map amplitude onto the -60..0 dBFS meter scale.
 *
 * @param amplitude Linear full-scale amplitude.
 * @return Clamped bar fraction in 0..1.
 */
static float level(float amplitude)
{
    return fmaxf(0, fminf(1, (AMPLITUDE_DB_FACTOR * log10f(fmaxf(amplitude, METER_AMPLITUDE_FLOOR)) + METER_RANGE_DB) / METER_RANGE_DB));
}

void ui_level_update(const float rms[2], int active, uint32_t now_ms)
{
    /* Smooth the displayed dB scale, with elapsed time so slower presets do not
     * change the response. */
    float elapsed_ms = meter_ready ? (uint32_t)(now_ms - meter_at) : 0;

    meter_at = now_ms;

    for (unsigned ch = 0; ch < 2; ++ch)
    {
        float target = active ? level(rms[ch]) : 0;

        if (!active || !meter_ready)
        {
            meter_rms[ch] = target;

            continue;
        }

        float response = target > meter_rms[ch] ? METER_RMS_ATTACK_MS : METER_RMS_RELEASE_MS;

        meter_rms[ch] += (target - meter_rms[ch]) * (1 - expf(-elapsed_ms / response));
    }

    meter_ready = active;
}

void ui_level_values(unsigned channel, float* rms)
{
    *rms = channel < 2 && meter_ready ? meter_rms[channel] : 0;
}
