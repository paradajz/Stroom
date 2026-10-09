#pragma once

#include "platform/graphics/constants.h"
#include <stdint.h>
#include <string.h>

#define SCENE_BATCH_HEADER_WORDS                 6
#define SCENE_LINE_REGISTERS                     4
#define SCENE_TEXTURED_TRIANGLE_REGISTERS        7
#define SCENE_SHADED_TEXTURED_TRIANGLE_REGISTERS 9

/**
 * @brief Calculate storage for a packed GS primitive batch.
 *
 * @param count Number of primitives.
 * @param registers Registers per primitive: 4 for lines, 3 for sprites, 7/9 for flat/shaded textured triangles.
 * @return Required number of 64-bit words, including headers and padding.
 */
static inline unsigned scene_batch_words(unsigned count, unsigned registers)
{
    return SCENE_BATCH_HEADER_WORDS + ((count * registers + 1) & ~1u);
}

/**
 * @brief Finalize headers and padding around vertices already in the queue.
 *
 * Write one packed PRIM followed by REGLIST vertices. Texture state is set by the caller.
 * Vertices already occupy the caller's reserved queue tail; this function only
 * fills headers and padding. The register layout must match PRIM shading:
 * flat textured triangles have one color, shaded triangles one per vertex.
 *
 * @param out Destination with scene_batch_words(count, registers) words.
 * @param prim Packed GS PRIM value.
 * @param count Number of primitives.
 * @param registers Registers per primitive: 4 for lines, 3 for sprites, 7/9 for flat/shaded textured triangles.
 */
static inline void scene_batch_finish(uint64_t* out, uint64_t prim, unsigned count, unsigned registers)
{
    out[0]                            = UINT64_C(1) | (UINT64_C(1) << PS2_GIF_NREG_SHIFT); /* PACKED AD, EOP=0 */
    out[1]                            = PS2_GIF_AD;
    out[2]                            = prim;
    out[3]                            = PS2_GIF_PRIM;
    out[4]                            = count | (UINT64_C(1) << PS2_GIF_EOP_SHIFT) | ((uint64_t)PS2_GIF_REGLIST << PS2_GIF_FORMAT_SHIFT) | ((uint64_t)registers << PS2_GIF_NREG_SHIFT);
    out[SCENE_BATCH_HEADER_WORDS - 1] = registers == SCENE_LINE_REGISTERS
                                            ? PS2_GIF_RGBAQ | (PS2_GIF_XYZ2 << PS2_GIF_REGISTER_BITS) | (PS2_GIF_RGBAQ << (2 * PS2_GIF_REGISTER_BITS)) | (PS2_GIF_XYZ2 << (3 * PS2_GIF_REGISTER_BITS))
                                            : PS2_GIF_RGBAQ | (PS2_GIF_XYZ2 << PS2_GIF_REGISTER_BITS) | (PS2_GIF_XYZ2 << (2 * PS2_GIF_REGISTER_BITS));

    if (registers == SCENE_TEXTURED_TRIANGLE_REGISTERS || registers == SCENE_SHADED_TEXTURED_TRIANGLE_REGISTERS)
    {
        uint64_t layout = 0;

        for (unsigned r = 0; r < registers; ++r)
        {
            int      color = registers == SCENE_SHADED_TEXTURED_TRIANGLE_REGISTERS ? r % 3 == 0 : r == 0;
            int      uv    = registers == SCENE_SHADED_TEXTURED_TRIANGLE_REGISTERS ? r % 3 == 1 : (r & 1) != 0;
            unsigned reg   = color ? PS2_GIF_RGBAQ : uv ? PS2_GIF_UV
                                                        : PS2_GIF_XYZ2;

            layout |= (uint64_t)reg << (r * PS2_GIF_REGISTER_BITS);
        }

        out[SCENE_BATCH_HEADER_WORDS - 1] = layout;
    }

    if (count * registers & 1)
    {
        out[SCENE_BATCH_HEADER_WORDS + count * registers] = 0;
    }
}

/** @brief Encode a packet from separately stored vertices (reference/test path). */
static inline void scene_batch_encode(uint64_t* out, uint64_t prim, unsigned count, unsigned registers, const uint64_t* vertices)
{
    scene_batch_finish(out, prim, count, registers);
    memcpy(out + SCENE_BATCH_HEADER_WORDS, vertices, count * registers * sizeof(*out));
}
