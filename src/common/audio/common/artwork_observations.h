#pragma once

#include "util/diagnostics.h"
#include "contracts/diagnostic.h"
#include "audio/common/metadata.h"
#include <stdint.h>

#if STROOM_DIAGNOSTICS
/** Artwork phase ID from the shared diagnostic contract. */
typedef uint32_t ArtworkPhase;

/** One completed operation; begin/end use wrapping EE milliseconds. */
typedef struct
{
    ArtworkPhase phase;     /**< Operation or explicit decode/upload beginning. */
    uint32_t     begin;     /**< Start time, including preemption. */
    uint32_t     end;       /**< End time. */
    uint32_t     values[3]; /**< Phase-specific dimensions, bytes or result. */
} ArtworkObservation;

/** Sink called by the owning thread; it must not retain the metadata pointer. */
typedef void (*ArtworkObserver)(const TrackMetadata* metadata, ArtworkObservation observation);
#endif
