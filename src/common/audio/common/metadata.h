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

/**
 * @brief Compare supported metadata fields, ignoring unused string capacity.
 * @param a First track metadata.
 * @param b Second track metadata.
 * @return Nonzero when title, artist, album and artwork URL agree.
 */
int track_metadata_equal(const TrackMetadata* a, const TrackMetadata* b);

/** CD covers have stable URLs; streaming senders may reuse URLs across tracks. */
typedef enum
{
    ARTWORK_PER_TRACK,
    ARTWORK_PER_URL
} ArtworkIdentity;

/**
 * @brief Compare artwork identity using the source-specific policy.
 * @param a First track metadata.
 * @param b Second track metadata.
 * @param identity Whether identity uses the URL alone or every metadata field.
 * @return Nonzero when the chosen identity fields agree.
 */
int track_artwork_equal(const TrackMetadata* a, const TrackMetadata* b, ArtworkIdentity identity);
