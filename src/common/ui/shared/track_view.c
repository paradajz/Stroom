#include "ui/shared/track_view.h"
#include "util/time_units.h"
#include <string.h>
#include "ui/shared/draw.h"
#include "ui/shared/text.h"
#include "ui/shared/track_layout.h"
#include "ui/artwork/view.h"

#define UTF8_CONTINUATION_MASK   0xc0
#define UTF8_CONTINUATION_TAG    0x80
#define UTF8_FOLD_MAX_BYTES      3u
#define ASCII_LIMIT              128
#define ELLIPSIS_LENGTH          3u
#define SCROLL_PAUSE_MS          1800u
#define SCROLL_PIXELS_PER_SECOND 24.0f
#define SCROLL_REENTRY_MS        250u

/* The bitmap font is ASCII. Fold Latin letters and typographic punctuation at display
 * time, leaving the original UTF-8 metadata intact for artwork identity. */
static char display_ascii(const char* start, unsigned bytes)
{
    static const struct
    {
        const char* letters;
        char        ascii;
    } folds[] = {
        { "‘’‚‛", '\'' }, { "“”„‟«»", '"' }, { "‐‑‒–—−", '-' }, { "ÀÁÂÃÄÅĀĂĄ", 'A' }, { "àáâãäåāăą", 'a' }, { "ÇĆĈĊČ", 'C' }, { "çćĉċč", 'c' }, { "ÐĎĐ", 'D' }, { "ðďđ", 'd' }, { "ÈÉÊËĒĔĖĘĚ", 'E' }, { "èéêëēĕėęě", 'e' }, { "ÌÍÎÏĨĪĬĮİ", 'I' }, { "ìíîïĩīĭįı", 'i' }, { "ŁĹĻĽ", 'L' }, { "łĺļľ", 'l' }, { "ÑŃŅŇ", 'N' }, { "ñńņň", 'n' }, { "ÒÓÔÕÖØŌŎŐ", 'O' }, { "òóôõöøōŏő", 'o' }, { "ŔŖŘ", 'R' }, { "ŕŗř", 'r' }, { "ŚŜŞŠ", 'S' }, { "śŝşš", 's' }, { "ŢŤŦ", 'T' }, { "ţťŧ", 't' }, { "ÙÚÛÜŨŪŬŮŰŲ", 'U' }, { "ùúûüũūŭůűų", 'u' }, { "ÝŶŸ", 'Y' }, { "ýÿŷ", 'y' }, { "ŹŻŽ", 'Z' }, { "źżž", 'z' }
    };

    if (bytes >= 2 && bytes <= UTF8_FOLD_MAX_BYTES)
    {
        char encoded[UTF8_FOLD_MAX_BYTES + 1];

        memcpy(encoded, start, bytes);

        encoded[bytes] = 0;

        for (unsigned i = 0; i < sizeof(folds) / sizeof(*folds); ++i)
        {
            if (strstr(folds[i].letters, encoded))
            {
                return folds[i].ascii;
            }
        }
    }

    return '?';
}

static void track_text(char* out, const char* text)
{
    unsigned used = 0;

    while (*text && used < METADATA_TEXT_BYTES - 1)
    {
        const char*   start = text;
        unsigned char c     = (unsigned char)*text++;

        if (c >= ASCII_LIMIT)
        {
            while (((unsigned char)*text & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_TAG)
            {
                ++text;
            }

            c = (unsigned char)display_ascii(start, (unsigned)(text - start));
        }

        if (c != ' ' && !ui_text_glyph(c))
        {
            c = '?';
        }

        out[used++] = (char)c;
    }

    out[used] = 0;
}

void ui_track_text_fit(char* out, const char* text, unsigned limit)
{
    char ascii[METADATA_TEXT_BYTES];

    track_text(ascii, text);

    if (limit >= UI_PLAYER_LABEL_BYTES)
    {
        limit = UI_PLAYER_LABEL_BYTES - 1;
    }

    unsigned count = (unsigned)strlen(ascii);

    if (count > limit)
    {
        count = limit;
    }

    memcpy(out, ascii, count);

    if (ascii[count] && count >= ELLIPSIS_LENGTH)
    {
        memset(out + count - ELLIPSIS_LENGTH, '.', ELLIPSIS_LENGTH);
    }

    out[count] = 0;
}

void ui_track_details_draw(GSGLOBAL* gs, const TrackMetadata* metadata, float bottom_offset, uint32_t now_ms)
{
    float cover_top = UI_PLAYER_COVER_TOP + bottom_offset;

    int artwork_drawn = ui_artwork_draw_at(gs, UI_PLAYER_COVER_LEFT, cover_top, UI_PLAYER_COVER_SIDE, UI_OVERLAY_ALPHA);

    if (!artwork_drawn && (metadata->title[0] || metadata->artist[0] || metadata->album[0]))
    {
        ui_rectangle(gs, UI_PLAYER_COVER_LEFT, cover_top, UI_PLAYER_COVER_SIDE, UI_PLAYER_COVER_SIDE, ui_color(UI_COLOR_BACKGROUND));

        artwork_drawn = 1;
    }

    if (artwork_drawn)
    {
        ui_border(gs, UI_PLAYER_COVER_LEFT, cover_top, UI_PLAYER_COVER_SIDE, UI_PLAYER_COVER_SIDE, UI_COLOR_TEXT);
    }

    const char* values[] = { metadata->title, metadata->artist, metadata->album };

    static struct
    {
        char     text[METADATA_TEXT_BYTES];
        uint32_t started, last_draw;
    } scroll[3];

    char text[METADATA_TEXT_BYTES];

    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); ++i)
    {
        track_text(text, values[i]);

        if (strcmp(text, scroll[i].text) != 0 || (uint32_t)(now_ms - scroll[i].last_draw) > SCROLL_REENTRY_MS)
        {
            strcpy(scroll[i].text, text);

            scroll[i].started = now_ms;
        }

        scroll[i].last_draw = now_ms;

        if (!text[0])
        {
            continue;
        }

        float overflow = ui_text_width(text, UI_PLAYER_TEXT_SCALE) - (UI_SAFE_RIGHT - UI_TRACK_METADATA_LEFT);
        float shift    = 0;

        if (overflow > 0)
        {
            uint32_t travel = (uint32_t)(overflow * MILLISECONDS_PER_SECOND / SCROLL_PIXELS_PER_SECOND) + 1;
            uint32_t phase  = (uint32_t)(now_ms - scroll[i].started) % (2 * (SCROLL_PAUSE_MS + travel));

            if (phase > SCROLL_PAUSE_MS)
            {
                uint32_t moving = phase - SCROLL_PAUSE_MS;

                if (moving <= travel)
                {
                    shift = overflow * moving / travel;
                }
                else if (moving <= travel + SCROLL_PAUSE_MS)
                {
                    shift = overflow;
                }
                else
                {
                    shift = overflow * (1.0f - (float)(moving - travel - SCROLL_PAUSE_MS) / travel);
                }
            }
        }

        if (overflow <= 0)
        {
            ui_label(gs, UI_TRACK_METADATA_LEFT, UI_TRACK_METADATA_TOP + i * UI_TRACK_METADATA_ROW_STEP + bottom_offset, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
        }
        else
        {
            ui_text_clipped(gs, UI_TRACK_METADATA_LEFT - shift, UI_TRACK_METADATA_TOP + i * UI_TRACK_METADATA_ROW_STEP + bottom_offset, text, ui_color(UI_COLOR_TEXT), UI_PLAYER_TEXT_SCALE, UI_TRACK_METADATA_LEFT, UI_SAFE_RIGHT);
        }
    }
}
