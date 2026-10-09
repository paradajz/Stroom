#include "ui/artwork/codecs.h"
#include "platform/graphics/constants.h"

int artwork_image_prepare(ArtworkImage* image, unsigned width, unsigned height)
{
#if STROOM_DIAGNOSTICS
    image->source_width  = width;
    image->source_height = height;
#endif

    if (!width || !height || width > ARTWORK_MAX_DIMENSION || height > ARTWORK_MAX_DIMENSION || width * height > ARTWORK_MAX_PIXELS)
    {
        image->too_large = 1;

        return ARTWORK_CODEC_ERROR_DIMENSIONS;
    }

    unsigned longest = width > height ? width : height;

    image->width  = width * ARTWORK_TEXTURE_SIDE / longest;
    image->height = height * ARTWORK_TEXTURE_SIDE / longest;

    if (!image->width)
    {
        image->width = 1;
    }

    if (!image->height)
    {
        image->height = 1;
    }

    for (unsigned i = 0; i < ARTWORK_TEXTURE_SIDE * ARTWORK_TEXTURE_SIDE; ++i)
    {
        image->pixels[i] = PS2_GS_OPAQUE_ALPHA;
    }

    return 0;
}
