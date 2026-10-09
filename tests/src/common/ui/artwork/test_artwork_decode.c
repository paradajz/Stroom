#include "ui/artwork/decode.h"
#include "unity.h"
#include <stdio.h>
#include <jpeglib.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

static ArtworkImage image;

/**
 * @brief No shared fixture initialization is required.
 */
void setUp(void)
{}

/**
 * @brief Cases release compressed fixtures themselves.
 */
void tearDown(void)
{}

/**
 * @brief Encode a red JPEG fixture using the real host JPEG library.
 * @param width Fixture width.
 * @param height Fixture height.
 * @param progressive Nonzero for progressive JPEG.
 * @param size Destination compressed size.
 * @return Heap buffer to free after use.
 */
static unsigned char* fixture(unsigned width, unsigned height, int progressive, unsigned long* size)
{
    struct jpeg_compress_struct jpeg;
    struct jpeg_error_mgr       error;

    jpeg.err = jpeg_std_error(&error);

    jpeg_create_compress(&jpeg);

    unsigned char* bytes = NULL;

    jpeg_mem_dest(&jpeg, &bytes, size);

    jpeg.image_width      = width;
    jpeg.image_height     = height;
    jpeg.input_components = 3;
    jpeg.in_color_space   = JCS_RGB;

    jpeg_set_defaults(&jpeg);

    if (progressive)
    {
        jpeg_simple_progression(&jpeg);
    }

    jpeg_start_compress(&jpeg, TRUE);

    unsigned char* row = calloc(width, 3);

    TEST_ASSERT_NOT_NULL(row);

    for (unsigned x = 0; x < width; ++x)
    {
        row[x * 3] = 255;
    }

    while (jpeg.next_scanline < height)
    {
        jpeg_write_scanlines(&jpeg, &row, 1);
    }

    jpeg_finish_compress(&jpeg);
    jpeg_destroy_compress(&jpeg);
    free(row);

    return bytes;
}

/**
 * @brief Real baseline and progressive JPEGs fit the cover box with correct GS color channels.
 */
static void decode(void)
{
    for (int progressive = 0; progressive < 2; ++progressive)
    {
        unsigned long  size  = 0;
        unsigned char* bytes = fixture(320, 160, progressive, &size);

        TEST_ASSERT_TRUE(artwork_decode(bytes, size, &image) == 0);
#if STROOM_DIAGNOSTICS
        TEST_ASSERT_EQUAL_UINT(320, image.source_width);
        TEST_ASSERT_EQUAL_UINT(160, image.source_height);
        TEST_ASSERT_EQUAL_UINT(1, image.format);
#endif
        TEST_ASSERT_EQUAL_UINT(128, image.width);
        TEST_ASSERT_EQUAL_UINT(64, image.height);
        TEST_ASSERT_GREATER_THAN_UINT(240, image.pixels[0] & 255);
        TEST_ASSERT_LESS_THAN_UINT(10, (image.pixels[0] >> 8) & 255);
        TEST_ASSERT_EQUAL_HEX32(0x80000000, image.pixels[128 * 65]);
        TEST_ASSERT_TRUE(!(artwork_decode(bytes, size / 2, &image) == 0));
        free(bytes);
    }

    unsigned long  size  = 0;
    unsigned char* bytes = fixture(8, 16, 0, &size);

    TEST_ASSERT_TRUE(artwork_decode(bytes, size, &image) == 0);
    TEST_ASSERT_EQUAL_UINT(64, image.width);
    TEST_ASSERT_EQUAL_UINT(128, image.height);
    free(bytes);

    bytes = fixture(2049, 1, 0, &size);

    TEST_ASSERT_TRUE(!(artwork_decode(bytes, size, &image) == 0));
    TEST_ASSERT_TRUE(image.too_large);
    free(bytes);
    TEST_ASSERT_TRUE(!(artwork_decode("invalid", 7, &image) == 0));
    TEST_ASSERT_FALSE(image.too_large);
    TEST_ASSERT_TRUE(!(artwork_decode(NULL, 0, &image) == 0));
}

/**
 * @brief PNG artwork is detected from its bytes, preserves proportions and composites alpha.
 */
static void decode_png(void)
{
    png_image png = { 0 };

    png.version = PNG_IMAGE_VERSION;
    png.width   = 2;
    png.height  = 1;
    png.format  = PNG_FORMAT_RGBA;

    const unsigned char pixels[] = { 255, 0, 0, 128, 0, 255, 0, 255 };
    png_alloc_size_t    size     = 0;

    TEST_ASSERT_TRUE(png_image_write_to_memory(&png, NULL, &size, 0, pixels, 0, NULL));

    unsigned char* bytes = malloc(size);

    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_TRUE(png_image_write_to_memory(&png, bytes, &size, 0, pixels, 0, NULL));
    TEST_ASSERT_TRUE(artwork_decode(bytes, size, &image) == 0);
    TEST_ASSERT_EQUAL_UINT(128, image.width);
    TEST_ASSERT_EQUAL_UINT(64, image.height);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(2, image.source_width);
    TEST_ASSERT_EQUAL_UINT(1, image.source_height);
    TEST_ASSERT_EQUAL_UINT(2, image.format);
#endif
    TEST_ASSERT_EQUAL_HEX32(0x80000080, image.pixels[0]);
    TEST_ASSERT_EQUAL_HEX32(0x8000ff00, image.pixels[127]);
    TEST_ASSERT_EQUAL_HEX32(0x80000000, image.pixels[128 * 65]);
    TEST_ASSERT_TRUE(!(artwork_decode(bytes, size / 2, &image) == 0));
    free(bytes);
    png_image_free(&png);
}

/**
 * @brief Run bounded image decoding regressions.
 * @return Failed case count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(decode);
    RUN_TEST(decode_png);

    return UNITY_END();
}
