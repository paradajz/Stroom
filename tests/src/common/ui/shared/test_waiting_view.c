#include "ui/waiting/view.h"
#include "ui/shared/draw.h"
#include "ui/shared/player_layout.h"
#include "unity.h"
#include <string.h>

static unsigned    labels;
static const char* expected_label;
static float       label_scale;

void setUp(void)
{
    labels         = 0;
    expected_label = "ERROR";
    label_scale    = 0;
}

void tearDown(void)
{}

float ui_text_width(const char* text, float scale)
{
    return strlen(text) * 10 * scale;
}

void ui_label(GSGLOBAL* gs, float x, float top, const char* text, float scale, UiColor color)
{
    (void)gs;
    (void)top;
    (void)color;
    TEST_ASSERT_EQUAL_UINT(0, labels);
    TEST_ASSERT_EQUAL_STRING(expected_label, text);

    label_scale = scale;

    TEST_ASSERT_TRUE(x >= 16);

    ++labels;
}

/** @brief Cleanup errors display their cause without stale connection hints. */
static void cleanup_error_shows_reason(void)
{
    AudioSourceStatus source = { .kind = AUDIO_SOURCE_ERROR, .network_ready = 1 };

    strcpy(source.network_address, "192.168.1.49");
    strcpy(source.error, "WORKER JOIN FAILED");

    expected_label = source.error;

    ui_waiting_draw(NULL, &source, UI_SCREEN_WAITING, 0);
    TEST_ASSERT_EQUAL_UINT(1, labels);
}

/** @brief Normal cleanup displays waiting without failure text or unavailable-network hints. */
static void pending_cleanup_is_neutral(void)
{
    AudioSourceStatus source = { .kind = AUDIO_SOURCE_WAITING, .network_waiting = 1, .network_ready = 1 };

    expected_label = "WAITING FOR AUDIO";

    strcpy(source.network_address, "192.168.1.49");
    ui_waiting_draw(NULL, &source, UI_SCREEN_WAITING, 0);
    TEST_ASSERT_EQUAL_UINT(1, labels);
}

static void pending_cleanup_shows_progress(void)
{
    AudioSourceStatus source = { .kind = AUDIO_SOURCE_WAITING };

    strcpy(source.error, "STOPPING NETWORK RECEIVER");

    expected_label = source.error;

    ui_waiting_draw(NULL, &source, UI_SCREEN_WAITING, 0);
    TEST_ASSERT_EQUAL_UINT(1, labels);
}

static void long_reason_fits_screen(void)
{
    AudioSourceStatus source = { .kind = AUDIO_SOURCE_ERROR };

    strcpy(source.error, "RESTART CONSOLE TO LOAD NETWORK MODULE");

    expected_label = source.error;

    ui_waiting_draw(NULL, &source, UI_SCREEN_WAITING, 0);
    TEST_ASSERT_EQUAL_UINT(1, labels);
    TEST_ASSERT_TRUE(label_scale > 0 && label_scale < UI_PLAYER_HEADING_SCALE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(cleanup_error_shows_reason);
    RUN_TEST(pending_cleanup_is_neutral);
    RUN_TEST(pending_cleanup_shows_progress);
    RUN_TEST(long_reason_fits_screen);

    return UNITY_END();
}
