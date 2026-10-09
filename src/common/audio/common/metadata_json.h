#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio/common/metadata.h"

/* A control byte expands to six JSON bytes (\u00XX). Reserve 128 bytes
 * for the envelope, field names, punctuation and terminator. */
#define TRACK_METADATA_JSON_BYTES (6 * (3 * METADATA_TEXT_BYTES + METADATA_ARTWORK_URL_BYTES) + 128)

/**
 * @brief Metadata request kind after validating a complete JSON document.
 */
typedef enum
{
    TRACK_METADATA_INVALID = METADATA_ACTION_INVALID, /**< Malformed JSON or unsupported field types. */
    TRACK_METADATA_UPDATE  = METADATA_ACTION_UPDATE,  /**< Merge supplied labels, preserving absent fields. */
    TRACK_METADATA_GET     = METADATA_ACTION_GET,     /**< Read current labels without changing them. */
    TRACK_METADATA_CLEAR   = METADATA_ACTION_CLEAR    /**< Clear all labels. */
} TrackMetadataAction;

/**
 * @brief Validate and atomically merge flat or data-wrapped metadata.
 * Duplicate labels across flat and wrapped fields are rejected.
 * @param metadata Labels to update; unchanged on invalid input or get.
 * @param json Complete JSON document, not necessarily terminated.
 * @param size Document byte count.
 * @return Parsed action, or INVALID without modifying labels.
 */
TrackMetadataAction track_metadata_apply_json(TrackMetadata* metadata, const void* json, size_t size);

/**
 * @brief Serialize the supported labels as the shared metadata JSON envelope.
 * @param metadata Labels to serialize.
 * @param json Destination of at least TRACK_METADATA_JSON_BYTES bytes.
 */
void track_metadata_to_json(const TrackMetadata* metadata, char* json);
