#include "audio/network/ariacast/diagnostics/diagnostic_capture.h"
#include "unity.h"
#include <string.h>

static AriaDiagnosticCapture diagnostic_capture;

/**
 * @brief Reset the recorder for each case.
 */
void setUp(void)
{
    memset(&diagnostic_capture, 0, sizeof(diagnostic_capture));
}

/**
 * @brief No resources need releasing.
 */
void tearDown(void)
{
}

/**
 * @brief Keep five seconds before silence and two after, then make the event immutable.
 */
static void capture_window(void)
{
    for (unsigned now = 0; now <= 6000; now += 10)
    {
        AriaDiagnosticCaptureRecord r = { .at = now, .kind = DIAGNOSTIC_CAPTURE_SAMPLE };

        aria_diagnostic_capture_record(&diagnostic_capture, r);
    }

    aria_diagnostic_capture_output(&diagnostic_capture, 6000, 7, 1);
    TEST_ASSERT_EQUAL_UINT(0, diagnostic_capture.events[0].id);
    aria_diagnostic_capture_output(&diagnostic_capture, 6000, 7, 0);
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.events[0].id);
    TEST_ASSERT_EQUAL_UINT(501, diagnostic_capture.events[0].count);
    TEST_ASSERT_EQUAL_UINT(1000, diagnostic_capture.events[0].records[0].at);
    aria_diagnostic_capture_record(&diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = 7500 });
    aria_diagnostic_capture_tick(&diagnostic_capture, 7999);
    TEST_ASSERT_FALSE(diagnostic_capture.events[0].complete);
    aria_diagnostic_capture_tick(&diagnostic_capture, 8000);
    TEST_ASSERT_TRUE(diagnostic_capture.events[0].complete);

    unsigned count = diagnostic_capture.events[0].count;

    aria_diagnostic_capture_record(&diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = 9000 });
    TEST_ASSERT_EQUAL_UINT(count, diagnostic_capture.events[0].count);

    char reply[2048];

    TEST_ASSERT_TRUE(aria_diagnostic_capture_reply(&diagnostic_capture, "STROOM_DIAGNOSTIC 1 0", reply, sizeof(reply)));
    TEST_ASSERT_NOT_NULL(strstr(reply, "[1000,"));
    TEST_ASSERT_TRUE(aria_diagnostic_capture_reply(&diagnostic_capture, "STROOM_DIAGNOSTIC 2 0", reply, sizeof(reply)));
    TEST_ASSERT_NOT_NULL(strstr(reply, "unavailable"));
}

/**
 * @brief Wraparound, reconnect and full slots do not erase retained events.
 */
static void retention_and_wrap(void)
{
    uint32_t at = UINT32_MAX - 100;

    aria_diagnostic_capture_record(&diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = at - 20 });
    aria_diagnostic_capture_output(&diagnostic_capture, at, 1, 0);
    aria_diagnostic_capture_tick(&diagnostic_capture, at + 2000u);
    TEST_ASSERT_TRUE(diagnostic_capture.events[0].complete);
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.events[0].count);
    aria_diagnostic_capture_session(&diagnostic_capture, 2);
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.events[0].id);

    for (unsigned i = 1; i <= 3; ++i)
    {
        aria_diagnostic_capture_session(&diagnostic_capture, i + 2);
        aria_diagnostic_capture_output(&diagnostic_capture, i * 10000, 2, 1);
        aria_diagnostic_capture_output(&diagnostic_capture, i * 10000, 2, 0);
        aria_diagnostic_capture_tick(&diagnostic_capture, i * 10000 + 2000);
    }

    TEST_ASSERT_EQUAL_UINT(3, diagnostic_capture.events[2].id);
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.missed);

    char reply[2048];

    aria_diagnostic_capture_reply(&diagnostic_capture, "STROOM_DIAGNOSTIC_CLEAR", reply, sizeof(reply));
    aria_diagnostic_capture_output(&diagnostic_capture, 40000, 2, 1);
    aria_diagnostic_capture_output(&diagnostic_capture, 40000, 2, 0);
    TEST_ASSERT_EQUAL_UINT(4, diagnostic_capture.events[0].id);
    TEST_ASSERT_EQUAL_UINT(0, diagnostic_capture.events[0].count);
}

/**
 * @brief Count truncation and preserve prehistory across a full rolling ring.
 */
static void bounded_overflow(void)
{
    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_RECORDS + 10; ++i)
    {
        aria_diagnostic_capture_record(&diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = 1000, .data = { i } });
    }

    aria_diagnostic_capture_output(&diagnostic_capture, 1000, 1, 0);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_CAPTURE_RECORDS, diagnostic_capture.events[0].count);
    TEST_ASSERT_EQUAL_UINT(10, diagnostic_capture.events[0].records[0].data[0]);
    TEST_ASSERT_TRUE(diagnostic_capture.events[0].history_limited);
    aria_diagnostic_capture_record(&diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = 1001 });
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.events[0].truncated);
}

/**
 * @brief Output stalls with ready PCM are captured separately from inserted silence.
 */
static void output_gap(void)
{
    aria_diagnostic_capture_output(&diagnostic_capture, 100, 1, 1);
    aria_diagnostic_capture_output(&diagnostic_capture, 250, 1, 1);
    TEST_ASSERT_EQUAL_UINT(1, diagnostic_capture.events[0].id);
    TEST_ASSERT_EQUAL_UINT(2, diagnostic_capture.events[0].reason);
    aria_diagnostic_capture_session(&diagnostic_capture, 2);
    aria_diagnostic_capture_output(&diagnostic_capture, 10000, 2, 1);
    TEST_ASSERT_EQUAL_UINT(0, diagnostic_capture.events[1].id);
}

/**
 * @brief Run bounded recorder regression cases.
 * @return Unity failure count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(capture_window);
    RUN_TEST(retention_and_wrap);
    RUN_TEST(bounded_overflow);
    RUN_TEST(output_gap);

    return UNITY_END();
}
