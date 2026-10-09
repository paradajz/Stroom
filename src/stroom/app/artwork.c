#include "app/artwork.h"
#include "recognition/client.h"
#include "audio/network/network.h"
#include "ui/artwork/view.h"
#include <stdlib.h>

int app_artwork_open(void)
{
    int result = app_artwork_close();

    if (result != 0)
    {
        return result;
    }

#if STROOM_DIAGNOSTICS
    ui_artwork_set_observer(network_artwork_observe);
#endif

    return ui_artwork_open() == 0 ? 0 : APP_ARTWORK_ERROR_START;
}

int app_artwork_close(void)
{
    int result = ui_artwork_close();

    if (result != 0)
    {
        return result < 0 ? APP_ARTWORK_ERROR_CLOSE : result;
    }

#if STROOM_DIAGNOSTICS
    ui_artwork_set_observer(NULL);
#endif

    return 0;
}

void app_artwork_prepare(const AudioSourceStatus* source)
{
    if ((source->kind == AUDIO_SOURCE_CD && source->cd.present) ||
        (source->kind == AUDIO_SOURCE_NETWORK && !source->network_waiting && !source->listening))
    {
        ArtworkBlob cover = { 0 };

        if (source->kind == AUDIO_SOURCE_NETWORK)
        {
            network_take_artwork(&source->metadata, &cover);
        }
        else
        {
            cd_lookup_take_artwork(&source->metadata, &cover);
        }

        ui_artwork_prepare(&source->metadata, &cover, source->kind == AUDIO_SOURCE_CD ? ARTWORK_PER_URL : ARTWORK_PER_TRACK);
        free(cover.data);
    }
    else
    {
        const TrackMetadata empty = { 0 };

        ui_artwork_prepare(&empty, NULL, ARTWORK_PER_TRACK);
    }
}
