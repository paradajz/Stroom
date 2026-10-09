#pragma once

/**
 * @brief Run the first two FFT stages on bit-reversed real input.
 *
 * Fuse sizes 2 and 4 while preserving quarter-turn constants and arithmetic order; no pre-zeroed imaginary buffer is needed.
 *
 * @param re Real buffer, updated in place.
 * @param im Imaginary output buffer of count floats.
 * @param count Transform size; must be a multiple of four.
 * @param c Quarter-turn cosine constant.
 * @param s Quarter-turn sine constant.
 */
static inline void fft_real_start(float* re, float* im, unsigned count, float c, float s)
{
    for (unsigned base = 0; base < count; base += 4)
    {
        float even0 = re[base] + re[base + 1];
        float odd0  = re[base] - re[base + 1];
        float even1 = re[base + 2] + re[base + 3];
        float odd1  = re[base + 2] - re[base + 3];
        float r     = c * odd1 - s * 0.0f;
        float v     = s * odd1 + c * 0.0f;

        re[base]     = even0 + even1;
        re[base + 2] = even0 - even1;
        re[base + 1] = odd0 + r;
        re[base + 3] = odd0 - r;
        im[base] = im[base + 2] = 0.0f;
        im[base + 1]            = 0.0f + v;
        im[base + 3]            = 0.0f - v;
    }
}
