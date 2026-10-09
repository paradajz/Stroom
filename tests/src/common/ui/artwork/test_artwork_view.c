#include "ui/artwork/view.h"
#include "ui/artwork/decode.h"
#include "ui/artwork/worker.h"
#include <stdlib.h>
#include "ui/shared/track_layout.h"
#include "ui/shared/style.h"
#include "unity.h"
#include <string.h>

static ArtworkBlob     pending;
static ArtworkImage    completed;
static TrackMetadata   completed_metadata;
static int             completion_ready, completion_valid;
static ArtworkObserver worker_observer;

static uint32_t           clock_ms;
static ArtworkObservation observations[20];
static unsigned           count, uploads, draws, allocations;
static u32                uploaded_vram;
static int                decode_ok, close_ok;
static float              drawn_left, drawn_top, drawn_right, drawn_bottom;
static unsigned           drawn_alpha;

uint32_t platform_millis(void)
{
    return clock_ms;
}

int artwork_decode(const void* bytes, size_t size, ArtworkImage* image)
{
    (void)bytes;
    (void)size;

    clock_ms += 200;
    image->width         = 128;
    image->height        = 96;
    image->source_width  = 640;
    image->source_height = 480;
    image->format        = 1;

    return decode_ok ? 0 : -1;
}

void artwork_worker_set_observer(ArtworkObserver sink)
{
    worker_observer = sink;
}

void artwork_worker_yield(uint32_t frame_started)
{
    (void)frame_started;
}

int artwork_worker_open(void)
{
    return 0;
}

int artwork_worker_close(void)
{
    if (!close_ok)
    {
        return -1;
    }

    free(pending.data);
    memset(&pending, 0, sizeof(pending));

    completion_ready = 0;

    return 0;
}

int artwork_worker_submit(ArtworkBlob* blob)
{
    free(pending.data);

    pending = *blob;

    memset(blob, 0, sizeof(*blob));

    return 0;
}

int artwork_worker_take(const TrackMetadata* metadata, ArtworkImage* image, int* valid, ArtworkIdentity identity)
{
    if (!completion_ready)
    {
        return 1;
    }

    completion_ready = 0;

    if (!track_artwork_equal(metadata, &completed_metadata, identity))
    {
        return 1;
    }

    *image = completed;
    *valid = completion_valid;

    return 0;
}

static void finish_decode(void)
{
    uint32_t begin = clock_ms;

    worker_observer(&pending.metadata, (ArtworkObservation){ DIAGNOSTIC_ARTWORK_DECODE_BEGIN, begin, begin, { pending.size, 0, 0 } });

    completion_valid = artwork_decode(pending.data, pending.size, &completed) == 0;

    worker_observer(&pending.metadata, (ArtworkObservation){ DIAGNOSTIC_ARTWORK_DECODE_END, begin, clock_ms, { completed.source_width, completed.source_height, completed.format | (completion_valid ? 0x100u : 0) } });
    worker_observer(&pending.metadata, (ArtworkObservation){ DIAGNOSTIC_ARTWORK_DECODE_RELEASE, clock_ms, clock_ms, { pending.size, 0, 0 } });

    completed_metadata = pending.metadata;
    completion_ready   = 1;

    free(pending.data);
    memset(&pending, 0, sizeof(pending));
}

u32 gsKit_texture_size(int width, int height, int psm)
{
    (void)psm;

    return (u32)(width * height * 4);
}

u32 gsKit_vram_alloc(GSGLOBAL* gs, u32 size, u8 type)
{
    (void)gs;
    (void)size;
    (void)type;

    clock_ms += 3;

    ++allocations;

    return 4096 * allocations;
}

void gsKit_texture_upload(GSGLOBAL* gs, GSTEXTURE* texture)
{
    (void)gs;
    TEST_ASSERT_EQUAL_UINT(128, texture->Width);

    clock_ms += 10;

    uploaded_vram = texture->Vram;

    ++uploads;
}

void gsKit_prim_sprite(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color)
{
    (void)gs;
    (void)x;
    (void)y;
    (void)right;
    (void)bottom;
    (void)z;
    (void)color;
}

void gsKit_prim_sprite_texture(GSGLOBAL* gs, const GSTEXTURE* texture, float x, float y, float u, float v, float right, float bottom, float uu, float vv, int z, u64 color)
{
    (void)gs;
    (void)texture;

    drawn_left = x;
    drawn_top  = y;

    (void)u;
    (void)v;

    drawn_right  = right;
    drawn_bottom = bottom;

    (void)uu;
    (void)vv;
    (void)z;

    drawn_alpha = (unsigned)((color >> 24) & 255);

    ++draws;
}

static void observe(const TrackMetadata* metadata, ArtworkObservation observation)
{
    TEST_ASSERT_TRUE(metadata->artwork_url[0]);
    TEST_ASSERT_LESS_THAN_UINT(20, count);

    observations[count++] = observation;
}

void setUp(void)
{
    ui_artwork_reset_texture();

    count = uploads = draws = allocations = 0;
    clock_ms                              = UINT32_MAX - 10;
    decode_ok                             = 1;
    close_ok                              = 1;

    ui_artwork_set_observer(observe);
    TEST_ASSERT_TRUE(ui_artwork_open() == 0);
}

void tearDown(void)
{
    ui_artwork_close();
    ui_artwork_set_observer(NULL);
}

/** @brief Measure first-use preparation across timer wrap; cached draws emit no duplicate uploads. */
static void cover_phases(void)
{
    GSGLOBAL    gs   = { 0 };
    ArtworkBlob blob = { .data = malloc(1), .size = 1 };

    TEST_ASSERT_NOT_NULL(blob.data);
    strcpy(blob.metadata.artwork_url, "http://1.2.3.4/cover");

    TrackMetadata metadata = blob.metadata;
    uint32_t      before   = clock_ms;

    ui_artwork_prepare(&metadata, &blob, ARTWORK_PER_URL);
    TEST_ASSERT_NULL(blob.data);
    TEST_ASSERT_EQUAL_UINT32(before, clock_ms);
    TEST_ASSERT_EQUAL_UINT(0, count);
    TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(0, uploads);
    TEST_ASSERT_EQUAL_STRING("DECODING", ui_artwork_status());
    finish_decode();
    ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_URL);
    TEST_ASSERT_EQUAL_UINT(3, count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_ARTWORK_DECODE_BEGIN, observations[0].phase);
    TEST_ASSERT_EQUAL_UINT32(200, observations[1].end - observations[1].begin);
    TEST_ASSERT_EQUAL_HEX32(0x101, observations[1].values[2]);
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(6, count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_ARTWORK_VRAM, observations[3].phase);
    TEST_ASSERT_EQUAL_UINT32(3, observations[3].end - observations[3].begin);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_ARTWORK_UPLOAD_END, observations[5].phase);
    TEST_ASSERT_EQUAL_UINT32(10, observations[5].end - observations[5].begin);
    ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_URL);
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(6, count);
    TEST_ASSERT_EQUAL_UINT(1, uploads);
    TEST_ASSERT_EQUAL_UINT(2, draws);
    TEST_ASSERT_EQUAL_UINT(128, drawn_alpha);
    ui_artwork_draw_at(&gs, 96, 380, 112, 115);
    TEST_ASSERT_EQUAL_FLOAT(96, drawn_left);
    TEST_ASSERT_EQUAL_FLOAT(394, drawn_top);
    TEST_ASSERT_EQUAL_FLOAT(208, drawn_right);
    TEST_ASSERT_EQUAL_FLOAT(478, drawn_bottom);
    TEST_ASSERT_EQUAL_UINT(115, drawn_alpha);
    ui_artwork_draw_at(&gs, 96, 380, 112, 200);
    TEST_ASSERT_EQUAL_UINT(128, drawn_alpha);
    TEST_ASSERT_EQUAL_UINT(1, uploads);

    TrackMetadata changed = metadata;

    strcpy(changed.title, "Next track");
    ui_artwork_prepare(&changed, NULL, ARTWORK_PER_URL);
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(1, uploads);
    TEST_ASSERT_EQUAL_UINT(6, count);
    /* Streaming must replace the cover even when the URL stays the same. */
    strcpy(changed.title, "Streaming next track");

    blob = (ArtworkBlob){ .data = malloc(1), .size = 1, .metadata = metadata };

    TEST_ASSERT_NOT_NULL(blob.data);
    ui_artwork_prepare(&changed, &blob, ARTWORK_PER_TRACK);
    TEST_ASSERT_NOT_NULL(blob.data);
    TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(5, draws);

    blob.metadata = changed;
    decode_ok     = 0;

    ui_artwork_prepare(&changed, &blob, ARTWORK_PER_TRACK);
    finish_decode();
    ui_artwork_prepare(&changed, NULL, ARTWORK_PER_TRACK);
    TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, UI_PLAYER_COVER_LEFT, UI_PLAYER_COVER_TOP, UI_PLAYER_COVER_SIDE, 128));
    TEST_ASSERT_EQUAL_UINT(9, count);
    TEST_ASSERT_EQUAL_HEX32(1, observations[7].values[2]);
    TEST_ASSERT_EQUAL_UINT(1, uploads);
    TEST_ASSERT_EQUAL_UINT(5, draws);
    TEST_ASSERT_EQUAL_STRING("IMAGE DECODE FAILED", ui_artwork_status());
}

/** @brief Aria refreshes reused URLs and uploads the next track's cover. */
static void streaming_reused_url(void)
{
    GSGLOBAL      gs       = { 0 };
    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://1.2.3.4/cover");

    for (unsigned track = 0; track < 2; ++track)
    {
        strcpy(metadata.title, track ? "Second" : "First");
        ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_TRACK);
        TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, 0, 0, 100, 128));

        ArtworkBlob blob = { .data = malloc(1), .size = 1, .metadata = metadata };

        TEST_ASSERT_NOT_NULL(blob.data);
        ui_artwork_prepare(&metadata, &blob, ARTWORK_PER_TRACK);
        TEST_ASSERT_NULL(blob.data);
        finish_decode();
        ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_TRACK);
        TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, 0, 0, 100, 128));
        TEST_ASSERT_EQUAL_UINT(track + 1, uploads);
    }
}

static void failed_close_preserves_cover(void)
{
    GSGLOBAL    gs    = { 0 };
    ArtworkBlob cover = { .data = malloc(1), .size = 1 };

    TEST_ASSERT_NOT_NULL(cover.data);
    strcpy(cover.metadata.artwork_url, "http://1.2.3.4/cover");

    TrackMetadata metadata = cover.metadata;

    ui_artwork_prepare(&metadata, &cover, ARTWORK_PER_URL);
    finish_decode();
    ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_URL);
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));

    close_ok = 0;

    TEST_ASSERT_TRUE(!(ui_artwork_close() == 0));
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));

    close_ok = 1;

    TEST_ASSERT_TRUE(ui_artwork_close() == 0);
    TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));
}

/** @brief Decoder reopening reuses VRAM, while context destruction invalidates it. */
static void reopen_reuses_texture_until_context_closes(void)
{
    GSGLOBAL      gs       = { 0 };
    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://1.2.3.4/cover");

    for (unsigned session = 0; session < 2; ++session)
    {
        ArtworkBlob cover = { .data = malloc(1), .size = 1, .metadata = metadata };

        TEST_ASSERT_NOT_NULL(cover.data);
        ui_artwork_prepare(&metadata, &cover, ARTWORK_PER_URL);
        finish_decode();
        ui_artwork_prepare(&metadata, NULL, ARTWORK_PER_URL);
        TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));
        TEST_ASSERT_EQUAL_UINT(1, allocations);
        TEST_ASSERT_EQUAL_UINT(4096, uploaded_vram);
        TEST_ASSERT_EQUAL_UINT(session + 1, uploads);

        if (!session)
        {
            TEST_ASSERT_TRUE(ui_artwork_close() == 0);
            TEST_ASSERT_TRUE(ui_artwork_open() == 0);
            TEST_ASSERT_FALSE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));
        }
    }

    /* Even reusing the same context address must upload to a fresh allocation. */
    ui_artwork_reset_texture();
    TEST_ASSERT_TRUE(ui_artwork_draw_at(&gs, 0, 0, 128, 128));
    TEST_ASSERT_EQUAL_UINT(2, allocations);
    TEST_ASSERT_EQUAL_UINT(8192, uploaded_vram);
    TEST_ASSERT_EQUAL_UINT(3, uploads);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(cover_phases);
    RUN_TEST(failed_close_preserves_cover);
    RUN_TEST(streaming_reused_url);
    RUN_TEST(reopen_reuses_texture_until_context_closes);

    return UNITY_END();
}
