#include "platform/graphics/display.h"
#include "unity.h"
#include <dmaKit.h>
#include <kernel.h>
#include <string.h>

static GSGLOBAL display;
static int (*refresh)(int);
static unsigned ticks, waits, flips, activations;
static int      token, race_refresh;
static int      live_context, fail_context, fail_sema, fail_handler, fail_delete;
static unsigned context_creations, handler_removals;
static unsigned deinitializations, semaphore_deletions, submission_step;

void setUp(void)
{
    memset(&display, 0, sizeof(display));

    ticks = waits = flips = activations = 0;
    token = race_refresh = 0;
    live_context = fail_context = fail_sema = fail_handler = 0;
    deinitializations = semaphore_deletions = submission_step = 0;
    refresh                                                   = NULL;
    fail_delete                                               = 0;
    context_creations = handler_removals = 0;
}

void tearDown(void)
{
    fail_delete = 0;

    TEST_ASSERT_TRUE(platform_display_close(NULL) == 0);
    TEST_ASSERT_FALSE(live_context);
}

GSGLOBAL* gsKit_init_global(void)
{
    if (fail_context)
    {
        return NULL;
    }

    TEST_ASSERT_FALSE(live_context);

    live_context = 1;

    ++context_creations;

    return &display;
}

void gsKit_init_screen(GSGLOBAL* gs)
{
    (void)gs;
}

void gsKit_mode_switch(GSGLOBAL* gs, int mode)
{
    (void)gs;
    (void)mode;
}

int gsKit_add_vsync_handler(int (*handler)(int))
{
    if (fail_handler)
    {
        return -1;
    }

    refresh = handler;

    return 1;
}

void ExitHandler(void)
{}

void dmaKit_init(int a, int b, int c, int d, int e, int f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
}

void dmaKit_chan_init(int channel)
{
    (void)channel;
}

static void next_refresh(void)
{
    ++ticks;
    refresh(0);
}

void gsKit_sync_flip(GSGLOBAL* gs)
{
    (void)gs;
    TEST_FAIL_MESSAGE("Presentation must not call the polling flip");
}

void gsKit_vsync_wait(void)
{
    TEST_FAIL_MESSAGE("Presentation must sleep instead of polling");
}

int CreateSema(ee_sema_t* sema)
{
    TEST_ASSERT_EQUAL_INT(0, sema->init_count);
    TEST_ASSERT_EQUAL_INT(1, sema->max_count);

    token = 0;

    return fail_sema ? -1 : 1;
}

int DeleteSema(int id)
{
    TEST_ASSERT_EQUAL_INT(1, id);
    TEST_ASSERT_NULL(refresh);
    ++semaphore_deletions;

    return fail_delete ? -7 : 0;
}

int iSignalSema(int id)
{
    TEST_ASSERT_EQUAL_INT(1, id);

    token = 1;

    return 0;
}

int WaitSema(int id)
{
    TEST_ASSERT_EQUAL_INT(1, id);
    ++waits;

    if (race_refresh)
    {
        race_refresh = 0;

        next_refresh(); /* Interrupt after the counter check, before sleep. */
    }

    if (!token)
    {
        next_refresh(); /* Simulate blocking until the interrupt. */
    }

    token = 0;

    return 0;
}

void gsKit_display_buffer(GSGLOBAL* gs)
{
    TEST_ASSERT_EQUAL_INT(flips & 1, gs->ActiveBuffer);
    ++flips;
}

void gsKit_setactive(GSGLOBAL* gs)
{
    TEST_ASSERT_EQUAL_INT(0, gs->PrimContext);
    TEST_ASSERT_EQUAL_INT(flips & 1, gs->ActiveBuffer);
    ++activations;
}

static void caps_fast_frames_without_delaying_slow_frames(void)
{
    TEST_ASSERT_NOT_NULL(platform_display_open());
    platform_display_present(&display, 2u);

    unsigned start = ticks;

    platform_display_present(&display, 2u);
    TEST_ASSERT_EQUAL_UINT(start + 2, ticks);

    start = ticks;

    next_refresh(); /* Rendering consumed one refresh. */
    platform_display_present(&display, 2u);
    TEST_ASSERT_EQUAL_UINT(start + 2, ticks);

    start = ticks;

    next_refresh();
    next_refresh(); /* Rendering missed the second refresh too. */
    platform_display_present(&display, 2u);
    TEST_ASSERT_EQUAL_UINT(start + 3, ticks);

    start = ticks;

    platform_display_present(&display, 2u); /* No catch-up burst after a slow frame. */
    TEST_ASSERT_EQUAL_UINT(start + 2, ticks);

    start = ticks;

    platform_display_present(&display, 1u);
    TEST_ASSERT_EQUAL_UINT(start + 1, ticks);

    start = ticks;

    platform_display_present(&display, 2u);
    TEST_ASSERT_EQUAL_UINT(start + 2, ticks);
}

/** @brief The adapter accepts caller-owned intervals beyond the UI's current caps. */
static void caller_controls_refresh_interval(void)
{
    TEST_ASSERT_NOT_NULL(platform_display_open());
    platform_display_present(&display, 1u);

    unsigned before = ticks;

    platform_display_present(&display, 3u);
    TEST_ASSERT_EQUAL_UINT(before + 3, ticks);

    before = ticks;

    platform_display_present(&display, 0u);
    TEST_ASSERT_EQUAL_UINT(before + 1, ticks);
}

static void first_frame_and_interrupt_race(void)
{
    TEST_ASSERT_NOT_NULL(platform_display_open());

    display.FirstFrame = 1;

    platform_display_present(&display, 2u);
    TEST_ASSERT_EQUAL_UINT(0, waits);
    TEST_ASSERT_EQUAL_UINT(0, flips);
    TEST_ASSERT_EQUAL_UINT(1, activations);

    display.FirstFrame = 0;
    race_refresh       = 1;

    platform_display_present(&display, 1u);
    TEST_ASSERT_EQUAL_UINT(1, ticks);
    TEST_ASSERT_EQUAL_UINT(1, waits);
    next_refresh(); /* Old notification must not cause an immediate flip. */

    unsigned before = ticks;

    platform_display_present(&display, 1u);
    TEST_ASSERT_EQUAL_UINT(before + 1, ticks);
    TEST_ASSERT_EQUAL_UINT(2, flips);
}

/** @brief Failed display startup releases only resources already acquired. */
static void startup_cleanup(void)
{
    fail_context = 1;

    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(0, deinitializations);

    fail_context = 0;
    fail_sema    = 1;

    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(1, deinitializations);
    TEST_ASSERT_EQUAL_UINT(0, semaphore_deletions);

    fail_sema    = 0;
    fail_handler = 1;

    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(2, deinitializations);
    TEST_ASSERT_EQUAL_UINT(1, semaphore_deletions);
}

/** @brief Submission finishes current GS work; close detaches interrupts before releasing storage. */
static void submission_and_close(void)
{
    TEST_ASSERT_NOT_NULL(platform_display_open());
    platform_display_submit(&display);
    TEST_ASSERT_EQUAL_UINT(2, submission_step);
    platform_display_close(&display);
    TEST_ASSERT_EQUAL_UINT(1, semaphore_deletions);
    TEST_ASSERT_EQUAL_UINT(1, deinitializations);
}

/** @brief Failed deletion retains the context and blocks replacement until cleanup completes. */
static void close_failure_retries(void)
{
    TEST_ASSERT_NOT_NULL(platform_display_open());

    fail_delete = 1;

    TEST_ASSERT_TRUE(!(platform_display_close(&display) == 0));
    TEST_ASSERT_TRUE(live_context);
    TEST_ASSERT_EQUAL_UINT(0, deinitializations);
    TEST_ASSERT_EQUAL_UINT(1, handler_removals);
    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(1, context_creations);
    TEST_ASSERT_TRUE(!(platform_display_close(&display) == 0));
    TEST_ASSERT_EQUAL_UINT(1, handler_removals);

    fail_delete = 0;

    TEST_ASSERT_TRUE(platform_display_close(&display) == 0);
    TEST_ASSERT_EQUAL_UINT(1, deinitializations);
    TEST_ASSERT_TRUE(platform_display_close(NULL) == 0);
    TEST_ASSERT_EQUAL_UINT(1, deinitializations);
    TEST_ASSERT_NOT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(2, context_creations);
}

/** @brief Failed startup retains its otherwise-unreturned context for close(NULL) retries. */
static void startup_deletion_failure_retries(void)
{
    fail_handler = fail_delete = 1;

    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_TRUE(live_context);
    TEST_ASSERT_EQUAL_UINT(0, deinitializations);
    TEST_ASSERT_EQUAL_UINT(1, semaphore_deletions);
    TEST_ASSERT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(1, context_creations);
    TEST_ASSERT_TRUE(!(platform_display_close(NULL) == 0));

    fail_delete = fail_handler = 0;

    TEST_ASSERT_TRUE(platform_display_close(NULL) == 0);
    TEST_ASSERT_FALSE(live_context);
    TEST_ASSERT_EQUAL_UINT(1, deinitializations);
    TEST_ASSERT_NOT_NULL(platform_display_open());
    TEST_ASSERT_EQUAL_UINT(2, context_creations);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(startup_cleanup);
    RUN_TEST(close_failure_retries);
    RUN_TEST(startup_deletion_failure_retries);
    RUN_TEST(submission_and_close);
    RUN_TEST(caps_fast_frames_without_delaying_slow_frames);
    RUN_TEST(first_frame_and_interrupt_race);
    RUN_TEST(caller_controls_refresh_interval);

    return UNITY_END();
}

void gsKit_queue_exec(GSGLOBAL* gs)
{
    TEST_ASSERT_EQUAL_PTR(&display, gs);
    TEST_ASSERT_EQUAL_UINT(0, submission_step++);
}

void gsKit_finish(void)
{
    TEST_ASSERT_EQUAL_UINT(1, submission_step++);
}

void gsKit_remove_vsync_handler(int handler)
{
    TEST_ASSERT_EQUAL_INT(1, handler);

    refresh = NULL;

    ++handler_removals;
}

void gsKit_deinit_global(GSGLOBAL* gs)
{
    TEST_ASSERT_EQUAL_PTR(&display, gs);
    TEST_ASSERT_NULL(refresh);
    TEST_ASSERT_TRUE(live_context);

    live_context = 0;

    ++deinitializations;
}
