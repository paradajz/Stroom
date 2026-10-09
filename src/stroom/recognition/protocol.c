#include "recognition/protocol.h"
#include "util/json.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum
{
    FIELD_PROTOCOL           = 1u << 0,
    FIELD_STATUS             = 1u << 1,
    FIELD_ID                 = 1u << 2,
    FIELD_RESULT_PATH        = 1u << 3,
    FIELD_RETRY              = 1u << 4,
    FIELD_COUNT              = 1u << 5,
    FIELD_ALBUM              = 1u << 6,
    FIELD_ARTIST             = 1u << 7,
    FIELD_TRACKS             = 1u << 8,
    FIELD_ARTWORK            = 1u << 9,
    FIELD_ERROR              = 1u << 10,
    FIELD_SERVICE            = 1u << 11,
    FIELD_READY              = 1u << 12,
    FIELD_EXPIRES            = 1u << 13,
    SERVICE_REQUIRED         = FIELD_PROTOCOL | FIELD_SERVICE | FIELD_READY,
    PENDING_REQUIRED         = FIELD_PROTOCOL | FIELD_STATUS | FIELD_ID | FIELD_RESULT_PATH | FIELD_RETRY,
    MATCHED_REQUIRED         = FIELD_PROTOCOL | FIELD_STATUS | FIELD_ID | FIELD_COUNT | FIELD_ALBUM | FIELD_ARTIST | FIELD_TRACKS | FIELD_ARTWORK,
    NO_MATCH_REQUIRED        = FIELD_PROTOCOL | FIELD_STATUS | FIELD_ID | FIELD_EXPIRES,
    ERROR_ENVELOPE_REQUIRED  = FIELD_PROTOCOL | FIELD_STATUS | FIELD_ERROR,
    TRACK_REQUIRED           = 1 | 2 | 4,
    ERROR_REQUIRED           = 1 | 2 | 4,
    ARTWORK_PENDING_REQUIRED = 1 | 8
};

/** Accept only finite, nonnegative integral JSON numbers in the wire range. */
static int integer(UtilJson* j, unsigned* out)
{
    util_json_space(j);

    const unsigned char* begin = j->at;

    if (j->at == j->end || (*j->at != '-' && (*j->at < '0' || *j->at > '9')) || !util_json_skip(j, 1))
    {
        return 0;
    }

    size_t n = (size_t)(j->at - begin);
    char   number[64];

    if (!n || n >= sizeof(number))
    {
        return 0;
    }

    memcpy(number, begin, n);

    number[n]    = 0;
    double value = strtod(number, NULL);

    if (!isfinite(value) || value < 0 || value > INT32_MAX || value != (unsigned)value)
    {
        return 0;
    }

    *out = (unsigned)value;

    return 1;
}

/**
 * @brief Parse a positive bounded delay and retain the larger existing value.
 * @param j JSON cursor.
 * @param out Current maximum delay in seconds, updated on success.
 * @return One for a valid delay, zero otherwise.
 */
static int bounded_seconds(UtilJson* j, uint32_t* out)
{
    unsigned seconds = 0;

    if (!integer(j, &seconds) || !seconds || seconds > CD_LOOKUP_MAX_RETRY_SECONDS)
    {
        return 0;
    }

    if (seconds > *out)
    {
        *out = seconds;
    }

    return 1;
}

/**
 * @brief Read a JSON object key and its separating colon.
 * @param j JSON cursor.
 * @param name Destination; oversized unknown names become an empty string.
 * @param capacity Destination capacity in bytes.
 * @return Nonzero on success, zero for malformed input.
 */
static int key(UtilJson* j, char* name, size_t capacity)
{
    int result = util_json_string(j, name, capacity);

    if (result == 2)
    {
        name[0] = 0; /* An unknown long name cannot alias a known field. */
    }

    return result && util_json_take(j, ':');
}

/**
 * @brief Reject a repeated known field while recording its presence.
 * @param seen Seen-field bitset to update.
 * @param field Nonzero field bit.
 * @return One for a newly seen field, zero for a duplicate.
 */
static int unique(unsigned* seen, unsigned field)
{
    if (*seen & field)
    {
        return 0;
    }

    *seen |= field;

    return 1;
}

/** Display labels contain no ASCII control characters after decoding. */
static int label(UtilJson* j, char* out)
{
    if (util_json_string(j, out, METADATA_TEXT_BYTES) != 1)
    {
        return 0;
    }

    for (const unsigned char* p = (const unsigned char*)out; *p; ++p)
    {
        if (*p < 32)
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Parse the bounded ordered track-label array.
 * @param j JSON cursor.
 * @param out Document receiving track labels.
 * @param count Receives the number of parsed tracks.
 * @return One on success, zero for malformed, duplicate or excessive track data.
 */
static int tracks(UtilJson* j, CdLookupDocument* out, unsigned* count)
{
    if (!util_json_take(j, '['))
    {
        return 0;
    }

    *count = 0;

    if (util_json_take(j, ']'))
    {
        return 1;
    }

    do
    {
        if (*count >= CD_MAX_TRACKS || !util_json_take(j, '{') || util_json_take(j, '}'))
        {
            return 0;
        }

        unsigned       seen = 0, number = 0;
        CdLookupTrack* track = &out->tracks[*count];

        do
        {
            char name[32];

            if (!key(j, name, sizeof(name)))
            {
                return 0;
            }

            unsigned field = !strcmp(name, "number") ? 1 : !strcmp(name, "title") ? 2
                                                       : !strcmp(name, "artist")  ? 4
                                                                                  : 0;

            if (field && !unique(&seen, field))
            {
                return 0;
            }

            if (field == 1)
            {
                if (!integer(j, &number))
                {
                    return 0;
                }
            }
            else if (field)
            {
                if (!label(j, field == 2 ? track->title : track->artist))
                {
                    return 0;
                }
            }
            else if (!util_json_skip(j, 1))
            {
                return 0;
            }

            if (util_json_take(j, '}'))
            {
                break;
            }

            if (!util_json_take(j, ','))
            {
                return 0;
            }
        } while (1);

        if (seen != TRACK_REQUIRED || number != *count + 1)
        {
            return 0;
        }

        ++*count;

        if (util_json_take(j, ']'))
        {
            return 1;
        }
    } while (util_json_take(j, ','));

    return 0;
}

/**
 * @brief Parse artwork status and its state-specific fields.
 * @param j JSON cursor.
 * @param out Artwork state destination.
 * @return One on success, zero for malformed or contradictory fields.
 */
static int artwork(UtilJson* j, CdLookupArtwork* out)
{
    if (!util_json_take(j, '{') || util_json_take(j, '}'))
    {
        return 0;
    }

    unsigned seen      = 0;
    char     state[16] = "";

    do
    {
        char name[32];

        if (!key(j, name, sizeof(name)))
        {
            return 0;
        }

        unsigned field = !strcmp(name, "status") ? 1 : !strcmp(name, "path")              ? 2
                                                   : !strcmp(name, "retry_after_seconds") ? 8
                                                                                          : 0;

        if (field && !unique(&seen, field))
        {
            return 0;
        }

        if (field == 1)
        {
            if (util_json_string(j, state, sizeof(state)) != 1)
            {
                return 0;
            }
        }
        else if (field == 2)
        {
            if (util_json_string(j, out->path, sizeof(out->path)) != 1)
            {
                return 0;
            }
        }
        else if (field == 8)
        {
            if (!bounded_seconds(j, &out->retry_seconds))
            {
                return 0;
            }
        }
        else if (!util_json_skip(j, 1))
        {
            return 0;
        }

        if (util_json_take(j, '}'))
        {
            break;
        }

        if (!util_json_take(j, ','))
        {
            return 0;
        }
    } while (1);

    if (!strcmp(state, "available") && seen == 3)
    {
        out->state = CD_LOOKUP_ARTWORK_AVAILABLE;
    }
    else if (!strcmp(state, "pending") && seen == ARTWORK_PENDING_REQUIRED)
    {
        out->state = CD_LOOKUP_ARTWORK_PENDING;
    }
    else if (!strcmp(state, "unavailable") && seen == 1)
    {
        out->state = CD_LOOKUP_ARTWORK_UNAVAILABLE;
    }
    else
    {
        return 0;
    }

    return 1;
}

/**
 * @brief Parse a bounded API error object and retryability flag.
 * @param j JSON cursor.
 * @param out Document receiving the error fields.
 * @return One on success, zero for missing, duplicate or invalid fields.
 */
static int error_object(UtilJson* j, CdLookupDocument* out)
{
    if (!util_json_take(j, '{') || util_json_take(j, '}'))
    {
        return 0;
    }

    unsigned seen = 0;

    do
    {
        char name[32];

        if (!key(j, name, sizeof(name)))
        {
            return 0;
        }

        unsigned field = !strcmp(name, "code") ? 1 : !strcmp(name, "message") ? 2
                                                 : !strcmp(name, "retryable") ? 4
                                                                              : 0;

        if (field && !unique(&seen, field))
        {
            return 0;
        }

        if (field == 1)
        {
            if (util_json_string(j, out->error, sizeof(out->error)) != 1)
            {
                return 0;
            }
        }
        else if (field == 2)
        {
            if (!util_json_string(j, NULL, 0))
            {
                return 0;
            }
        }
        else if (field == 4)
        {
            if (util_json_literal(j, "true"))
            {
                out->retryable = 1;
            }
            else if (!util_json_literal(j, "false"))
            {
                return 0;
            }
        }
        else if (!util_json_skip(j, 1))
        {
            return 0;
        }

        if (util_json_take(j, '}'))
        {
            return seen == ERROR_REQUIRED;
        }
    } while (util_json_take(j, ','));

    return 0;
}

/**
 * @brief Validate the fixed-length opaque MusicBrainz-style disc identifier.
 * @param id Null-terminated identifier.
 * @return Nonzero when length, alphabet and terminal hyphen are valid.
 */
static int valid_id(const char* id)
{
    if (strlen(id) != CD_LOOKUP_ID_BYTES - 1 || id[CD_LOOKUP_ID_BYTES - 2] != '-')
    {
        return 0;
    }

    for (unsigned i = 0; i < CD_LOOKUP_ID_BYTES - 2; ++i)
    {
        unsigned c = (unsigned char)id[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Validate an available cover path within the local covers route.
 * @param out Document artwork state.
 * @return Nonzero for unavailable/pending artwork or a valid local JPEG path.
 */
static int artwork_path(const CdLookupDocument* out)
{
    if (out->artwork.state == CD_LOOKUP_ARTWORK_UNAVAILABLE)
    {
        return 1;
    }

    if (out->artwork.state == CD_LOOKUP_ARTWORK_PENDING)
    {
        return 1;
    }

    const char* path = out->artwork.path;
    size_t      n    = strlen(path);

    if ((strncmp(path, "/covers/", 8) != 0) || n < sizeof("/covers/.jpg") || (strcmp(path + n - 4, ".jpg") != 0))
    {
        return 0;
    }

    for (const char* p = path + 8; *p; ++p)
    {
        unsigned c = (unsigned char)*p;

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
        {
            return 0;
        }
    }

    return 1;
}

int cd_lookup_document(CdLookupDocument* out, const void* data, size_t size)
{
    memset(out, 0, sizeof(*out));

    if (size > CD_LOOKUP_RESPONSE_BYTES)
    {
        return 0;
    }

    UtilJson j = { data, (const unsigned char*)data + size, UTIL_JSON_TEXT_EXACT };

    if (!util_json_take(&j, '{') || util_json_take(&j, '}'))
    {
        return 0;
    }

    unsigned seen = 0, protocol = 0, track_count = 0;
    char     status[16] = "", service[32] = "";
    int      ready = 0;

    const char* const names[] = { "protocol", "status", "disc_id", "result_path", "retry_after_seconds", "track_count", "album", "artist", "tracks", "artwork", "error", "service", "ready", "expires_in_seconds" };

    do
    {
        char name[32];

        if (!key(&j, name, sizeof(name)))
        {
            return 0;
        }

        unsigned field = 0;

        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            if (!strcmp(name, names[i]))
            {
                field = 1u << i;
            }
        }

        if (field && !unique(&seen, field))
        {
            return 0;
        }

        switch (field)
        {
        case FIELD_PROTOCOL:

            if (!integer(&j, &protocol))
            {
                return 0;
            }

            break;
        case FIELD_STATUS:

            if (util_json_string(&j, status, sizeof(status)) != 1)
            {
                return 0;
            }

            break;
        case FIELD_ID:

            if (util_json_string(&j, out->id, sizeof(out->id)) != 1)
            {
                return 0;
            }

            break;
        case FIELD_RESULT_PATH:

            if (util_json_string(&j, out->result_path, sizeof(out->result_path)) != 1)
            {
                return 0;
            }

            break;
        case FIELD_RETRY:

            if (!bounded_seconds(&j, &out->retry_seconds))
            {
                return 0;
            }

            break;
        case FIELD_EXPIRES:

            if (!bounded_seconds(&j, &out->expires_seconds))
            {
                return 0;
            }

            break;
        case FIELD_COUNT:

            if (!integer(&j, &out->count))
            {
                return 0;
            }

            break;
        case FIELD_ALBUM:

            if (!label(&j, out->album))
            {
                return 0;
            }

            break;
        case FIELD_ARTIST:

            if (!label(&j, out->artist))
            {
                return 0;
            }

            break;
        case FIELD_TRACKS:

            if (!tracks(&j, out, &track_count))
            {
                return 0;
            }

            break;
        case FIELD_ARTWORK:

            if (!artwork(&j, &out->artwork))
            {
                return 0;
            }

            break;
        case FIELD_ERROR:

            if (!error_object(&j, out))
            {
                return 0;
            }

            break;
        case FIELD_SERVICE:

            if (util_json_string(&j, service, sizeof(service)) != 1)
            {
                return 0;
            }

            break;
        case FIELD_READY:

            if (!util_json_literal(&j, "true"))
            {
                return 0;
            }

            ready = 1;

            break;
        default:

            if (!util_json_skip(&j, 1))
            {
                return 0;
            }
        }

        if (util_json_take(&j, '}'))
        {
            break;
        }

        if (!util_json_take(&j, ','))
        {
            return 0;
        }
    } while (1);
    util_json_space(&j);

    if (j.at != j.end || protocol != 2 || !(seen & 1))
    {
        return 0;
    }

    if ((seen & 4) && !valid_id(out->id))
    {
        return 0;
    }

    if ((seen & SERVICE_REQUIRED) == SERVICE_REQUIRED && ready && !strcmp(service, "stroom-cd-recognition"))
    {
        out->kind = CD_DOCUMENT_SERVICE;
    }
    else if (!strcmp(status, "pending") && (seen & PENDING_REQUIRED) == PENDING_REQUIRED)
    {
        char expected[64];

        snprintf(expected, sizeof(expected), "/cd/v2/lookups/%s", out->id);

        if ((strcmp(expected, out->result_path) != 0))
        {
            return 0;
        }

        out->kind = CD_DOCUMENT_PENDING;
    }
    else if (!strcmp(status, "matched") && (seen & MATCHED_REQUIRED) == MATCHED_REQUIRED && out->count >= 1 && out->count <= CD_MAX_TRACKS && out->count == track_count && artwork_path(out))
    {
        out->kind = CD_DOCUMENT_MATCHED;
    }
    else if (!strcmp(status, "no_match") && (seen & NO_MATCH_REQUIRED) == NO_MATCH_REQUIRED)
    {
        out->kind = CD_DOCUMENT_NO_MATCH;
    }
    else if (!strcmp(status, "error") && (seen & ERROR_ENVELOPE_REQUIRED) == ERROR_ENVELOPE_REQUIRED && (!(out->retryable) || (seen & 16)))
    {
        out->kind = CD_DOCUMENT_ERROR;
    }

    return out->kind != CD_DOCUMENT_INVALID;
}
