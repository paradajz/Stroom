#include "util/diagnostics.h"
#include <string.h>
#include "ui/artwork/view.h"
#include "ui/artwork/decode.h"
#include "ui/artwork/worker.h"
#include "ui/shared/style.h"

#if STROOM_DIAGNOSTICS
#include "platform/time/clock.h"
#endif

static ArtworkIdentity current_identity;

static ArtworkImage  image __attribute__((aligned(64)));
static GSTEXTURE     texture;
static TrackMetadata current_metadata;
static int           ready;
static int           uploaded;
static int           allocation_failed;
static int           decoder_available;
#if STROOM_DIAGNOSTICS
static int             decoded, submitted;
static ArtworkObserver artwork_observer;

void ui_artwork_set_observer(ArtworkObserver observer)
{
    artwork_observer = observer;

    artwork_worker_set_observer(observer);
}

/** @brief Emit one phase using the identity of the image being prepared. */
static void observe(ArtworkPhase phase, uint32_t begin, uint32_t end, unsigned a, unsigned b, unsigned c)
{
    if (artwork_observer)
    {
        artwork_observer(&current_metadata, (ArtworkObservation){ phase, begin, end, { a, b, c } });
    }
}
#endif

int ui_artwork_open(void)
{
    if (artwork_worker_open() != 0)
    {
        return UI_ARTWORK_ERROR_DECODER_START;
    }

    memset(&current_metadata, 0, sizeof(current_metadata));

    ready = uploaded = allocation_failed = 0;
#if STROOM_DIAGNOSTICS
    decoded = submitted = 0;
#endif
    decoder_available = 1;

    return 0;
}

int ui_artwork_close(void)
{
    int result = artwork_worker_close();

    if (result != 0)
    {
        return result < 0 ? UI_ARTWORK_ERROR_DECODER_CLOSE : result;
    }

    decoder_available = ready = uploaded = 0;

    return 0;
}

void ui_artwork_reset_texture(void)
{
    memset(&texture, 0, sizeof(texture));

    uploaded = allocation_failed = 0;
}

void ui_artwork_yield(uint32_t frame_started)
{
    artwork_worker_yield(frame_started);
}

void ui_artwork_prepare(const TrackMetadata* metadata, ArtworkBlob* blob, ArtworkIdentity identity)
{
    if (identity != current_identity || !track_artwork_equal(metadata, &current_metadata, identity))
    {
        ready = uploaded = 0;
#if STROOM_DIAGNOSTICS
        decoded = submitted = 0;
#endif
    }

    current_identity = identity;
    current_metadata = *metadata;

    int valid;

    if (artwork_worker_take(&current_metadata, &image, &valid, identity) == 0)
    {
        ready    = valid;
        uploaded = 0;
#if STROOM_DIAGNOSTICS
        decoded = 1;
#endif
    }

    if (!ready && blob && blob->data && track_artwork_equal(&blob->metadata, &current_metadata, identity) && current_metadata.artwork_url[0])
    {
        if (artwork_worker_submit(blob) == 0)
        {
#if STROOM_DIAGNOSTICS
            submitted = 1;
#endif
        }
    }
}

int ui_artwork_draw_at(GSGLOBAL* gs, float cover_left, float cover_top, float cover_side, unsigned opacity)
{
    if (!ready || allocation_failed)
    {
        return 0;
    }

    if (!texture.Vram)
    {
        texture.Width = texture.Height = ARTWORK_TEXTURE_SIDE;
        texture.PSM                    = GS_PSM_CT32;
        texture.Filter                 = GS_FILTER_LINEAR;
        texture.TBW                    = ARTWORK_TEXTURE_SIDE / 64;
        texture.Mem                    = (u32*)image.pixels;
#if STROOM_DIAGNOSTICS
        uint32_t begin = platform_millis();
#endif
        texture.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(texture.Width, texture.Height, texture.PSM), GSKIT_ALLOC_SYSBUFFER);

#if STROOM_DIAGNOSTICS
        uint32_t end = platform_millis();

        observe(DIAGNOSTIC_ARTWORK_VRAM, begin, end, ARTWORK_TEXTURE_SIDE * ARTWORK_TEXTURE_SIDE * 4, texture.Vram != GSKIT_ALLOC_ERROR, 0);
#endif

        if (texture.Vram == GSKIT_ALLOC_ERROR)
        {
            allocation_failed = 1;
            texture.Vram      = 0;

            return 0;
        }
    }

    if (!uploaded)
    {
#if STROOM_DIAGNOSTICS
        uint32_t begin = platform_millis();

        observe(DIAGNOSTIC_ARTWORK_UPLOAD_BEGIN, begin, begin, ARTWORK_TEXTURE_SIDE * ARTWORK_TEXTURE_SIDE * 4, 0, 0);

        begin = platform_millis();
#endif
        gsKit_texture_upload(gs, &texture);
#if STROOM_DIAGNOSTICS
        uint32_t end = platform_millis();

        observe(DIAGNOSTIC_ARTWORK_UPLOAD_END, begin, end, ARTWORK_TEXTURE_SIDE * ARTWORK_TEXTURE_SIDE * 4, 0, 0);
#endif
        uploaded = 1;
    }

    float width  = (float)image.width * cover_side / ARTWORK_TEXTURE_SIDE;
    float height = (float)image.height * cover_side / ARTWORK_TEXTURE_SIDE;
    float left   = cover_left + (cover_side - width) / 2;
    float top    = cover_top + (cover_side - height) / 2;

    gsKit_prim_sprite(gs, cover_left, cover_top, cover_left + cover_side, cover_top + cover_side, 1, ui_color(UI_COLOR_BACKGROUND));
    gsKit_prim_sprite_texture(gs, &texture, left, top, 0.5f, 0.5f, left + width, top + height, image.width - 0.5f, image.height - 0.5f, 1, (ui_color(UI_COLOR_TEXTURE_NEUTRAL) & ~(0xffULL << 24)) | ((u64)(opacity > 128 ? 128 : opacity) << 24));

    return 1;
}

#if STROOM_DIAGNOSTICS
const char* ui_artwork_status(void)
{
    return !current_metadata.artwork_url[0] ? "NO URL" : allocation_failed        ? "VRAM ALLOCATION FAILED"
                                                     : uploaded                   ? "DRAWN"
                                                     : ready                      ? "DECODED"
                                                     : decoded && image.too_large ? "IMAGE DIMENSIONS TOO LARGE"
                                                     : decoded                    ? "IMAGE DECODE FAILED"
                                                     : !decoder_available         ? "DECODER UNAVAILABLE"
                                                     : submitted                  ? "DECODING"
                                                                                  : "WAITING FOR DOWNLOAD";
}
#endif
