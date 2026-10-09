#include "ui/artwork/codecs.h"
#include "audio/artwork/artwork.h"
#include <string.h>

#define JPEG_MARKER_PREFIX  0xff
#define JPEG_START_OF_IMAGE 0xd8

int artwork_decode(const void* bytes, size_t size, ArtworkImage* image)
{
    static const unsigned char png_signature[] = "\x89PNG\r\n\x1a\n";

    image->too_large = size > ARTWORK_MAX_BYTES;
#if STROOM_DIAGNOSTICS
    image->source_width = image->source_height = image->format = 0;
#endif

    if (!bytes || !size || image->too_large)
    {
        return ARTWORK_DECODE_ERROR_INVALID_DATA;
    }

    if (size >= (sizeof(png_signature) - 1) && memcmp(bytes, png_signature, (sizeof(png_signature) - 1)) == 0)
    {
#if STROOM_DIAGNOSTICS
        image->format = 2;
#endif
        return artwork_decode_png(bytes, size, image);
    }

#if STROOM_DIAGNOSTICS
    const unsigned char* signature = bytes;

    image->format = size >= 2 && signature[0] == JPEG_MARKER_PREFIX && signature[1] == JPEG_START_OF_IMAGE ? 1 : 0;
#endif

    return artwork_decode_jpeg(bytes, size, image);
}
