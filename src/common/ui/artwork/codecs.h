#pragma once

#include "ui/artwork/decode.h"

/** Failure codes for this API. */
typedef enum
{
    ARTWORK_CODEC_ERROR_INVALID_DATA = ARTWORK_DECODE_ERROR_INVALID_DATA,
    ARTWORK_CODEC_ERROR_DIMENSIONS   = ARTWORK_DECODE_ERROR_DIMENSIONS,
    ARTWORK_CODEC_ERROR_MEMORY       = ARTWORK_DECODE_ERROR_MEMORY,
    ARTWORK_CODEC_ERROR_JPEG         = ARTWORK_DECODE_ERROR_JPEG,
    ARTWORK_CODEC_ERROR_PNG          = ARTWORK_DECODE_ERROR_PNG,
} ArtworkCodecError;

#define ARTWORK_MAX_DIMENSION 2048
#define ARTWORK_MAX_PIXELS    (1024u * 1024u)

/**
 * @brief Validate source dimensions, fit the image and clear unused texture pixels.
 * @param image Destination pixels and fitted image dimensions.
 * @param width Source width in pixels.
 * @param height Source height in pixels.
 * @return Zero on success, ARTWORK_CODEC_ERROR_DIMENSIONS for unsupported source dimensions.
 */
int artwork_image_prepare(ArtworkImage* image, unsigned width, unsigned height);

/**
 * @brief Decode JPEG data after format selection.
 * @param bytes Compressed image.
 * @param size Image byte count.
 * @param image Destination pixels.
 * @return 0 on success, a negative ArtworkCodecError on failure.
 */
int artwork_decode_jpeg(const void* bytes, size_t size, ArtworkImage* image);

/**
 * @brief Decode PNG data after format selection.
 * @param bytes Compressed image.
 * @param size Image byte count.
 * @param image Destination pixels.
 * @return 0 on success, a negative ArtworkCodecError on failure.
 */
int artwork_decode_png(const void* bytes, size_t size, ArtworkImage* image);
