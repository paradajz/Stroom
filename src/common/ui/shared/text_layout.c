#include "ui/shared/text_layout.h"
#include <string.h>

float ui_text_width(const char* text, float scale)
{
    size_t length = strlen(text);

    return length ? (length * UI_TEXT_ADVANCE - UI_TEXT_GAP) * scale : 0;
}
