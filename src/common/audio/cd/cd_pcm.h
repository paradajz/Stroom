#pragma once

#include "audio/cd/cd_format.h"
#include "contracts/audio.h"
#include <stdint.h>

/* Stereo PCM16 has four bytes per frame; one sector spans 1/75 second. */
#define CD_INPUT_FRAMES  (CD_SECTOR_BYTES / 4)
#define CD_OUTPUT_FRAMES (AUDIO_RATE / CD_SECTORS_PER_SECOND)
#define CD_OUTPUT_BYTES  (CD_OUTPUT_FRAMES * 4)

/**
 * @brief Interpolation history carried between consecutive CD sectors.
 */
typedef struct
{
    int16_t previous[2]; /**< Last source sample for each stereo channel. */
    int     primed;      /**< Nonzero once previous samples are initialized. */
} CdResampler;

/**
 * @brief Convert one CD sector to stereo PCM16 at the shared playback rate.
 *
 * @param state Interpolation history; zero-initialize at a discontinuity.
 * @param sector CD_SECTOR_BYTES of little-endian stereo PCM16.
 * @param output Destination of CD_OUTPUT_BYTES bytes.
 */
void cd_resample(CdResampler* state, const uint8_t* sector, uint8_t* output);
