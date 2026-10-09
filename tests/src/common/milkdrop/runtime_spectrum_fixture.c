#include "milkdrop/milk.h"
#include "milkdrop/milk_wave_points.h"

static void nothing(float* values, uint32_t* random);

MILK_WAVE_POINT_LOOP(wave_points, (void)v;)

static const MilkObjectProgram wave = {
    .type     = 1,
    .defaults = { [MO_SPECTRUM] = 1, [MO_SAMPLES] = 16, [MO_SCALING] = 1, [MO_A] = 1 },
    .init     = nothing,
    .frame    = nothing,
    .points   = wave_points,
};

/* Exercise both transition directions even when the playback selection has no spectrum waves. */
const MilkProgram milk_programs[] = {
    { .name = "No spectrum", .init = nothing, .frame = nothing, .vertex = nothing },
    { .name = "Spectrum wave", .defaults = { [ML_WAVE_SCALE] = 1 }, .init = nothing, .frame = nothing, .vertex = nothing, .object_count = 1, .objects = &wave, .custom_spectrum = 1 },
};

static void nothing(float* values, uint32_t* random)
{
    (void)values;
    (void)random;
}
