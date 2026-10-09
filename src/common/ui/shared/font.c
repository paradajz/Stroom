#include "ui/shared/font.h"
#include <stddef.h>

#define FONT_DIGITS 10

/* Compact 3x5 uppercase display font, rendered as GS rectangles. */
static const char* const glyphs[] = {
    "111101101101111",
    "010110010010111",
    "111001111100111",
    "111001111001111",
    "101101111001001",
    "111100111001111",
    "111100111101111",
    "111001010010010",
    "111101111101111",
    "111101111001111",
    "010101111101101",
    "110101110101110",
    "111100100100111",
    "110101101101110",
    "111100110100111",
    "111100110100100",
    "111100101101111",
    "101101111101101",
    "111010010010111",
    "001001001101111",
    "101101110101101",
    "100100100100111",
    "101111111101101",
    "101111111111101",
    "111101101101111",
    "111101111100100",
    "111101101111001",
    "110101110101101",
    "111100111001111",
    "111010010010010",
    "101101101101111",
    "101101101101010",
    "101101111111101",
    "101101010101101",
    "101101010010010",
    "111001010100111",
};

const char* ui_text_glyph(unsigned char ch)
{
    if (ch >= 'a' && ch <= 'z')
    {
        ch -= 'a' - 'A';
    }

    int         index = ch >= '0' && ch <= '9' ? ch - '0' : (ch >= 'A' && ch <= 'Z' ? ch - 'A' + FONT_DIGITS : -1);
    const char* glyph = index >= 0 ? glyphs[index] : NULL;

    switch (ch)
    {
    case ' ':
        break;
    case '.':
        glyph = "000000000000100";

        break;
    case ',':
        glyph = "000000000010100";

        break;
    case '-':
        glyph = "000000111000000";

        break;
    case '_':
        glyph = "000000000000111";

        break;
    case '+':
        glyph = "000010111010000";

        break;
    case '&':
        glyph = "010101010101011";

        break;
    case '(':
        glyph = "010100100100010";

        break;
    case ')':
        glyph = "010001001001010";

        break;
    case '[':
        glyph = "110100100100110";

        break;
    case ']':
        glyph = "011001001001011";

        break;
    case '\'':
        glyph = "010010000000000";

        break;
    case '"':
        glyph = "101101000000000";

        break;
    case '/':
        glyph = "001001010100100";

        break;
    case '>':
        glyph = "100010001010100";

        break;
    case ':':
        glyph = "000010000010000";

        break;
    case '!':
        glyph = "010010010000010";

        break;
    case '?':
        glyph = "110001010000010";

        break;
    case '#':
        glyph = "101111101111101";

        break;
    }

    return glyph;
}
