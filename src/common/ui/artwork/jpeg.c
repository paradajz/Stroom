#include "ui/artwork/codecs.h"
#include "platform/graphics/constants.h"
#include "audio/artwork/artwork.h"
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>

#define ARTWORK_JPEG_MEMORY (4L * 1024L * 1024L)

/**
 * @brief JPEG error manager with a recoverable failure destination.
 */
typedef struct
{
    struct jpeg_error_mgr manager; /**< Library error callbacks. */
    jmp_buf               jump;    /**< Returns decoder errors to the caller. */
} JpegError;

/**
 * @brief Heap-owned decoder state remains valid after longjmp.
 */
typedef struct
{
    struct jpeg_decompress_struct decoder; /**< JPEG allocation and scanline state. */
    JpegError                     error;   /**< Recoverable failure handler. */
} JpegContext;

/**
 * @brief Recover from invalid JPEG data instead of terminating the application.
 * @param info JPEG context.
 */
static void jpeg_failed(j_common_ptr info)
{
    JpegError* error = (JpegError*)info->err;

    longjmp(error->jump, 1);
}

/**
 * @brief Keep malformed optional artwork from flooding console output.
 * @param info JPEG context, unused.
 */
static void jpeg_message(j_common_ptr info)
{
    (void)info;
}

int artwork_decode_jpeg(const void* bytes, size_t size, ArtworkImage* image)
{
    if (!bytes || !size || size > ARTWORK_MAX_BYTES)
    {
        return ARTWORK_CODEC_ERROR_INVALID_DATA;
    }

    JpegContext* ctx = calloc(1, sizeof(*ctx));

    if (!ctx)
    {
        return ARTWORK_CODEC_ERROR_MEMORY;
    }

    ctx->decoder.err                  = jpeg_std_error(&ctx->error.manager);
    ctx->error.manager.error_exit     = jpeg_failed;
    ctx->error.manager.output_message = jpeg_message;

    if (setjmp(ctx->error.jump))
    {
        jpeg_destroy_decompress(&ctx->decoder);
        free(ctx);
        return ARTWORK_CODEC_ERROR_JPEG;
    }

    jpeg_create_decompress(&ctx->decoder);

    ctx->decoder.mem->max_memory_to_use = ARTWORK_JPEG_MEMORY;

    jpeg_mem_src(&ctx->decoder, bytes, size);
    jpeg_read_header(&ctx->decoder, TRUE);

    unsigned width  = ctx->decoder.image_width;
    unsigned height = ctx->decoder.image_height;

    if (artwork_image_prepare(image, width, height) != 0)
    {
        jpeg_destroy_decompress(&ctx->decoder);
        free(ctx);
        return ARTWORK_CODEC_ERROR_DIMENSIONS;
    }

    unsigned longest = width > height ? width : height;

    ctx->decoder.scale_num   = 1;
    ctx->decoder.scale_denom = 1;

    while (ctx->decoder.scale_denom < 8 && longest / (ctx->decoder.scale_denom * 2) >= ARTWORK_TEXTURE_SIDE)
    {
        ctx->decoder.scale_denom *= 2;
    }

    ctx->decoder.out_color_space = JCS_RGB;

    jpeg_start_decompress(&ctx->decoder);

    JSAMPARRAY row = (*ctx->decoder.mem->alloc_sarray)((j_common_ptr)&ctx->decoder, JPOOL_IMAGE, ctx->decoder.output_width * 3, 1);
    unsigned   y   = 0;

    while (ctx->decoder.output_scanline < ctx->decoder.output_height)
    {
        unsigned source_y = ctx->decoder.output_scanline;

        jpeg_read_scanlines(&ctx->decoder, row, 1);

        while (y < image->height && y * ctx->decoder.output_height / image->height == source_y)
        {
            for (unsigned x = 0; x < image->width; ++x)
            {
                unsigned at = x * ctx->decoder.output_width / image->width * 3;

                image->pixels[y * ARTWORK_TEXTURE_SIDE + x] = PS2_GS_OPAQUE_ALPHA | (uint32_t)row[0][at] | ((uint32_t)row[0][at + 1] << 8) | ((uint32_t)row[0][at + 2] << 16);
            }

            ++y;
        }
    }

    jpeg_finish_decompress(&ctx->decoder);

    int valid = ctx->error.manager.num_warnings == 0;

    jpeg_destroy_decompress(&ctx->decoder);
    free(ctx);

    return valid ? 0 : ARTWORK_CODEC_ERROR_JPEG;
}
