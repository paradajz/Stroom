#pragma once

#include "contracts/cd.h"
#include "audio/cd/cd_format.h"

/** Encode exact MusicBrainz sector offsets (including the 150-sector lead-in).
 * Returns bytes written, or zero for invalid TOC/insufficient capacity. */
unsigned cd_lookup_encode(const CdToc* toc, unsigned generation, char* data, unsigned capacity);
