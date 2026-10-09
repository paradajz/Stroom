#include "ui/artwork/codecs.h"
#include "platform/graphics/constants.h"
#include <png.h>
#include <stdlib.h>

#define PNG_CHANNELS 4

int artwork_decode_png(const void* bytes, size_t size, ArtworkImage* image)
{
    png_image png = { 0 };

    png.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&png, bytes, size))
    {
        png_image_free(&png);
        return ARTWORK_CODEC_ERROR_PNG;
    }

    unsigned width  = png.width;
    unsigned height = png.height;

    if (artwork_image_prepare(image, width, height) != 0)
    {
        png_image_free(&png);
        return ARTWORK_CODEC_ERROR_DIMENSIONS;
    }

    png.format = PNG_FORMAT_RGBA;

    unsigned char* pixels = malloc(PNG_IMAGE_SIZE(png));

    if (!pixels)
    {
        png_image_free(&png);
        return ARTWORK_CODEC_ERROR_MEMORY;
    }

    if (!png_image_finish_read(&png, NULL, pixels, 0, NULL))
    {
        free(pixels);
        png_image_free(&png);
        return ARTWORK_CODEC_ERROR_PNG;
    }

    for (unsigned y = 0; y < image->height; ++y)
    {
        for (unsigned x = 0; x < image->width; ++x)
        {
            unsigned at    = ((y * height / image->height) * width + x * width / image->width) * PNG_CHANNELS;
            unsigned alpha = pixels[at + 3];
            uint32_t red   = pixels[at] * alpha / UINT8_MAX;
            uint32_t green = pixels[at + 1] * alpha / UINT8_MAX;
            uint32_t blue  = pixels[at + 2] * alpha / UINT8_MAX;

            image->pixels[y * ARTWORK_TEXTURE_SIDE + x] = PS2_GS_OPAQUE_ALPHA | red | (green << 8) | (blue << 16);
        }
    }

    free(pixels);
    png_image_free(&png);

    return 0;
}
