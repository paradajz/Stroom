#pragma once

#include "contracts/metadata.h"

/**
 * @brief Bounded UTF-8 track labels shared by audio sources and presentation.
 */
typedef struct
{
    char title[METADATA_TEXT_BYTES];              /**< Track title; empty when unknown. */
    char artist[METADATA_TEXT_BYTES];             /**< Artist; empty when unknown. */
    char album[METADATA_TEXT_BYTES];              /**< Album; empty when unknown. */
    char artwork_url[METADATA_ARTWORK_URL_BYTES]; /**< HTTP artwork URL; empty when absent. */
} TrackMetadata;

/** Compare supported fields, ignoring unused string capacity. */
int track_metadata_equal(const TrackMetadata* a, const TrackMetadata* b);

/** CD covers have stable URLs; streaming senders may reuse URLs across tracks. */
typedef enum
{
    ARTWORK_PER_TRACK,
    ARTWORK_PER_URL
} ArtworkIdentity;

/** Compare artwork using the source-specific identity policy. */
int track_artwork_equal(const TrackMetadata* a, const TrackMetadata* b, ArtworkIdentity identity);
