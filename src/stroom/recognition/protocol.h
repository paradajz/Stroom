#pragma once

#include "contracts/cd.h"
#include "audio/common/metadata.h"
#include <stddef.h>
#include <stdint.h>

#define CD_LOOKUP_ID_BYTES    29
#define CD_LOOKUP_ERROR_BYTES 48
#define CD_LOOKUP_PATH_BYTES  METADATA_ARTWORK_URL_BYTES

typedef enum
{
    CD_DOCUMENT_INVALID,
    CD_DOCUMENT_SERVICE,
    CD_DOCUMENT_PENDING,
    CD_DOCUMENT_MATCHED,
    CD_DOCUMENT_NO_MATCH,
    CD_DOCUMENT_ERROR
} CdDocumentKind;

typedef struct
{
    unsigned state;
    char     path[CD_LOOKUP_PATH_BYTES];
    uint32_t retry_seconds;
} CdLookupArtwork;

typedef struct
{
    char title[METADATA_TEXT_BYTES];
    char artist[METADATA_TEXT_BYTES];
} CdLookupTrack;

/** Whole-disc reply. Allocate off the worker stack; validate before publishing. */
typedef struct
{
    CdDocumentKind  kind;
    char            id[CD_LOOKUP_ID_BYTES];
    char            result_path[64];
    uint32_t        retry_seconds;
    uint32_t        expires_seconds;
    unsigned        count;
    char            album[METADATA_TEXT_BYTES];
    char            artist[METADATA_TEXT_BYTES];
    CdLookupTrack   tracks[CD_MAX_TRACKS];
    CdLookupArtwork artwork;
    char            error[CD_LOOKUP_ERROR_BYTES];
    int             retryable;
} CdLookupDocument;

/**
 * @brief Parse a complete, bounded UTF-8 API response document.
 * @param out Destination for validated document fields.
 * @param data Response bytes; need not be null-terminated.
 * @param size Response byte count.
 * @return Nonzero for a valid supported document, zero for invalid input.
 */
int cd_lookup_document(CdLookupDocument* out, const void* data, size_t size);
