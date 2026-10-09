#include "audio/common/metadata.h"
#include <string.h>

int track_metadata_equal(const TrackMetadata* a, const TrackMetadata* b)
{
    return !strcmp(a->title, b->title) && !strcmp(a->artist, b->artist) && !strcmp(a->album, b->album) && !strcmp(a->artwork_url, b->artwork_url);
}

int track_artwork_equal(const TrackMetadata* a, const TrackMetadata* b, ArtworkIdentity identity)
{
    return identity == ARTWORK_PER_URL ? strcmp(a->artwork_url, b->artwork_url) == 0 : track_metadata_equal(a, b);
}
