#pragma once

#include "util/diagnostics.h"
#include <stddef.h>
#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    ARTWORK_DECODE_ERROR_INVALID_DATA = -1,
    ARTWORK_DECODE_ERROR_DIMENSIONS   = -2,
    ARTWORK_DECODE_ERROR_MEMORY       = -3,
    ARTWORK_DECODE_ERROR_JPEG         = -4,
    ARTWORK_DECODE_ERROR_PNG          = -5,
} ArtworkDecodeError;

#define ARTWORK_TEXTURE_SIDE 128

/**
 * @brief Fixed-size decoded cover suitable for a PS2 CT32 texture.
 */
typedef struct
{
    uint32_t pixels[ARTWORK_TEXTURE_SIDE * ARTWORK_TEXTURE_SIDE]; /**< RGB with GS opaque alpha; unused area is black. */
#if STROOM_DIAGNOSTICS
    unsigned source_width;  /**< Original width before scaling. */
    unsigned source_height; /**< Original height before scaling. */
    unsigned format;        /**< Zero unknown, one JPEG, two PNG. */
#endif
    int      too_large; /**< Decode failed because dimensions or compressed size exceed limits. */
    unsigned width;     /**< Fitted image width, at most ARTWORK_TEXTURE_SIDE. */
    unsigned height;    /**< Fitted image height, at most ARTWORK_TEXTURE_SIDE. */
} ArtworkImage;

/**
 * @brief Decode bounded JPEG or PNG artwork and fit its aspect ratio into a 128-pixel square.
 * @param bytes Compressed image data; format is detected from bytes.
 * @param size Compressed byte count.
 * @param image Destination; do not display it on failure.
 * @return 0 on success, a negative ArtworkDecodeError on failure.
 */
int artwork_decode(const void* bytes, size_t size, ArtworkImage* image);
