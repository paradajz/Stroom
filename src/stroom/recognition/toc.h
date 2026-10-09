#pragma once

#include "contracts/cd.h"
#include "audio/cd/cd_format.h"

/**
 * @brief Encode a TOC with MusicBrainz offsets including the 150-sector lead-in.
 * @param toc Validated audio-disc layout.
 * @param data Destination for compact UTF-8 request JSON.
 * @param capacity Destination capacity in bytes.
 * @return Bytes written, or zero for an invalid TOC or insufficient capacity.
 */
unsigned cd_lookup_encode(const CdToc* toc, char* data, unsigned capacity);
