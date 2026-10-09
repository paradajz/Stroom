#pragma once

#include "contracts/artwork.h"
#include <stdint.h>
#include "audio/common/metadata.h"

/**
 * @brief Completed compressed image and the metadata snapshot identifying its track.
 *
 * A successful handoff transfers data ownership; the consumer frees it after use.
 */
typedef struct
{
    uint8_t*      data;     /**< Heap buffer; consumer must free it. */
    unsigned      size;     /**< Compressed byte count. */
    TrackMetadata metadata; /**< Track labels and URL identifying the downloaded cover. */
} ArtworkBlob;
