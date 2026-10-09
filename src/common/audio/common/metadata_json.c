#include "audio/common/metadata_json.h"
#include "util/json.h"
#include "util/parse.h"
#include <stdint.h>
#include <string.h>

/**
 * @brief Read supported fields from an object, optionally unwrapping data.
 * @param j Parser cursor.
 * @param metadata Pending labels.
 * @param type Destination request type.
 * @param root Nonzero for the outer object.
 * @param seen Fields already supplied anywhere in this update.
 * @return Nonzero on valid input.
 */
static int object(UtilJson* j, TrackMetadata* metadata, char* type, int root, unsigned* seen)
{
    if (!util_json_take(j, '{'))
    {
        return 0;
    }

    if (util_json_take(j, '}'))
    {
        return 1;
    }

    do
    {
        char key[32];

        if (!util_json_string(j, key, sizeof(key)) || !util_json_take(j, ':'))
        {
            return 0;
        }

        unsigned field = !strcmp(key, "title") ? 1 : !strcmp(key, "artist")     ? 2
                                                 : !strcmp(key, "album")        ? 4
                                                 : root && !strcmp(key, "data") ? 8
                                                 : root && !strcmp(key, "type") ? 16
                                                                                : 0;

        if (!strcmp(key, "artworkUrl") || !strcmp(key, "artwork_url"))
        {
            field = 32;
        }

        if (field && (*seen & field))
        {
            return 0;
        }

        *seen |= field;

        if (field == 8)
        {
            if (!object(j, metadata, type, 0, seen))
            {
                return 0;
            }
        }
        else if (field == 16)
        {
            if (!util_json_string(j, type, 16))
            {
                return 0;
            }
        }
        else if (field)
        {
            char* out = field == 32 ? metadata->artwork_url : field == 1 ? metadata->title
                                                          : field == 2   ? metadata->artist
                                                                         : metadata->album;

            if (util_json_literal(j, "null"))
            {
                out[0] = 0;
            }
            else if (field == 32 ? util_json_string(j, out, METADATA_ARTWORK_URL_BYTES) != 1 : !util_json_string(j, out, METADATA_TEXT_BYTES))
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
            return 1;
        }
    } while (util_json_take(j, ','));

    return 0;
}

TrackMetadataAction track_metadata_apply_json(TrackMetadata* metadata, const void* json, size_t size)
{
    UtilJson      j        = { json, (const unsigned char*)json + size, UTIL_JSON_TEXT_DISPLAY };
    TrackMetadata pending  = *metadata;
    char          type[16] = "";

    unsigned seen = 0;

    if (!object(&j, &pending, type, 1, &seen))
    {
        return TRACK_METADATA_INVALID;
    }

    util_json_space(&j);

    if (j.at != j.end)
    {
        return TRACK_METADATA_INVALID;
    }

    if (!strcmp(type, "get"))
    {
        return TRACK_METADATA_GET;
    }

    if (!strcmp(type, "clear"))
    {
        memset(metadata, 0, sizeof(*metadata));
        return TRACK_METADATA_CLEAR;
    }

    if (type[0] && strcmp(type, "update") != 0 && strcmp(type, "metadata") != 0)
    {
        return TRACK_METADATA_INVALID;
    }

    *metadata = pending;

    return TRACK_METADATA_UPDATE;
}

/**
 * @brief Append a label, escaping JSON controls without temporary label buffers.
 * @param out Destination with room for six bytes per input byte.
 * @param text Terminated UTF-8 label.
 * @return Next output position.
 */
static char* escape(char* out, const char* text)
{
    static const char hex[] = "0123456789abcdef";

    while (*text)
    {
        unsigned char c = (unsigned char)*text++;

        if (c < ' ')
        {
            *out++ = '\\';
            *out++ = 'u';
            *out++ = '0';
            *out++ = '0';
            *out++ = hex[c >> 4];
            *out++ = hex[c & UTIL_HEX_DIGIT_MASK];
        }
        else
        {
            if (c == '"' || c == '\\')
            {
                *out++ = '\\';
            }

            *out++ = (char)c;
        }
    }

    return out;
}

void track_metadata_to_json(const TrackMetadata* metadata, char* json)
{
    const char* labels[]   = { metadata->title, metadata->artist, metadata->album, metadata->artwork_url };
    const char* prefixes[] = { "{\"type\":\"metadata\",\"data\":{\"title\":\"", "\",\"artist\":\"", "\",\"album\":\"", "\",\"artwork_url\":\"" };
    char*       out        = json;

    for (unsigned i = 0; i < sizeof(labels) / sizeof(*labels); ++i)
    {
        size_t size = strlen(prefixes[i]);

        memcpy(out, prefixes[i], size);

        out = escape(out + size, labels[i]);
    }

    strcpy(out, "\"}}");
}
