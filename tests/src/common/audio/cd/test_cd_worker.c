/* Exercise the real worker with SDK and host sleep substitutes. */
#include "audio/cd/cd.h"
#include "audio/cd/cd_transport.h"
#include "audio/cd/cd_controller.h"
#include "audio/output/device.h"
#include "support/worker_driver.h"
#include <unistd.h>
#include <string.h>
#include "unity.h"
#include <kernel.h>

int        _gp;
static int semaphores[3];
static int next_semaphore;

static int       selected, steps, sleeps, stop_after;
static CdRuntime callbacks;
static void (*detect_hook)(CdController*);
static void (*step_hook)(CdController*, const CdRequests*);
static void (*sleep_hook)(void);
static int configured_autoplay;
static int status_failure, drive_start_failure, drive_restart_required;
static int start_failure, thread_delete_failure, sema_delete_failure;

int __wrap_usleep(useconds_t duration)
{
    (void)duration;
    ++sleeps;

    if (sleeps == 1)
    {
        selected = 1;
    }

    if (sleeps >= stop_after)
    {
        test_worker_stop();
    }

    if (sleep_hook)
    {
        sleep_hook();
    }

    return 0;
}

int output_selected(OutputOwner owner)
{
    return selected && owner == OUTPUT_CD;
}

int CreateSema(ee_sema_t* sema)
{
    int id = ++next_semaphore;

    TEST_ASSERT_LESS_THAN_INT(3, id);

    semaphores[id] = sema->init_count;

    return id;
}

int WaitSema(int id)
{
    TEST_ASSERT_EQUAL_INT(1, semaphores[id]);
    --semaphores[id];

    return 0;
}

int SignalSema(int id)
{
    semaphores[id] = 1;

    return 0;
}

int DeleteSema(int id)
{
    if (id == sema_delete_failure)
    {
        return -23;
    }

    semaphores[id] = 0;

    return 0;
}

int CreateThread(ee_thread_t* thread)
{
    TEST_ASSERT_NOT_NULL(thread->func);

    return 1;
}

int StartThread(int id, void* arg)
{
    (void)id;
    (void)arg;

    return start_failure ? -17 : 0;
}

int DeleteThread(int id)
{
    (void)id;

    return thread_delete_failure ? -23 : 0;
}

void ExitThread(void)
{}

uint32_t platform_millis(void)
{
    return 100;
}

void cd_controller_open(CdController* c, const CdRuntime* runtime, uint32_t now, int autoplay)
{
    memset(c, 0, sizeof(*c));

    c->drive_ready             = !drive_start_failure && !drive_restart_required;
    c->status.present          = c->drive_ready;
    c->status.restart_required = drive_restart_required;

    if (drive_start_failure)
    {
        strcpy(c->status.error, "CD DRIVE INIT FAILED");
    }

    if (drive_restart_required)
    {
        strcpy(c->status.error, "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
    }

    callbacks           = *runtime;
    configured_autoplay = autoplay;

    (void)now;
}

void cd_controller_detect(CdController* c, uint32_t now)
{
    if (detect_hook)
    {
        detect_hook(c);
    }

    (void)now;
}

void cd_controller_step(CdController* c, const CdRequests* requests, uint32_t now)
{
    ++steps;

    if (step_hook)
    {
        step_hook(c, requests);
    }

    (void)now;
}

void cd_controller_close(CdController* c)
{
    (void)c;
}

static void replacement_script(CdController* c)
{
    c->status = (CdPlaybackStatus){ .present = 1, .generation = sleeps ? 2 : 1, .playing = !sleeps };

    callbacks.stage(&c->status, "Disc changed");

    Audio            audio;
    CdPlaybackStatus observed;

    if (!sleeps)
    {
        const uint8_t pcm[] = { 17, 0, 17, 0 };

        callbacks.pcm(pcm, 1);
        cd_copy_snapshot(&observed, &audio);
        TEST_ASSERT_EQUAL_UINT(1, audio.history_count);
        TEST_ASSERT_EQUAL_INT16(17, audio.history[0][0]);
        cd_transport_command(1, AUDIO_TRANSPORT_NEXT);
    }
    else
    {
        cd_copy_snapshot(&observed, &audio);
        TEST_ASSERT_EQUAL_UINT(2, observed.generation);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
        cd_transport_command(1, AUDIO_TRANSPORT_NEXT);
    }
}

static void no_old_requests(CdController* c, const CdRequests* requests)
{
    (void)c;
    TEST_ASSERT_FALSE(requests->have_command);
}

static void replacement_publication(void)
{
    detect_hook = replacement_script;
    step_hook   = no_old_requests;

    test_worker_run();
    TEST_ASSERT_EQUAL_INT(1, steps);
}

static void between_reads_script(CdController* c)
{
    c->status = (CdPlaybackStatus){ .present = 1, .generation = 1 };

    callbacks.stage(&c->status, "First disc");

    CdPlaybackStatus observed;
    Audio            audio;

    cd_poll(&observed);
    TEST_ASSERT_EQUAL_UINT(1, observed.generation);

    c->status.generation = 2;

    callbacks.stage(&c->status, "Replacement");

    const uint8_t pcm[] = { 42, 0, 42, 0 };

    callbacks.pcm(pcm, 1);
    cd_copy_snapshot(&observed, &audio);
    TEST_ASSERT_EQUAL_UINT(2, observed.generation);
    TEST_ASSERT_EQUAL_UINT(1, audio.history_count);
    TEST_ASSERT_EQUAL_INT16(42, audio.history[0][0]);
    callbacks.stage(&c->status, "Still ready");
    cd_copy_snapshot(&observed, &audio);
    TEST_ASSERT_EQUAL_UINT(1, audio.history_count);

    c->status = (CdPlaybackStatus){ 0 };

    callbacks.stage(&c->status, "No disc");
    cd_copy_snapshot(&observed, &audio);
    TEST_ASSERT_FALSE(observed.present);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
}

static void replacement_between_reads(void)
{
    detect_hook = between_reads_script;

    test_worker_run();
}

static void unavailable_snapshot(void)
{
    TEST_ASSERT_EQUAL_INT(0, cd_close());

    Audio            audio;
    CdPlaybackStatus status;

    memset(&audio, 0x7f, sizeof(audio));
    memset(&status, 0x7f, sizeof(status));
    cd_copy_snapshot(&status, &audio);
    TEST_ASSERT_FALSE(status.present);
    TEST_ASSERT_EQUAL_UINT(0, status.generation);
    TEST_ASSERT_EQUAL_STRING("CD WORKER START FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
}

void setUp(void)
{
    test_worker_reset();

    drive_restart_required = 0;
    next_semaphore         = 0;
    selected = steps = sleeps = status_failure = drive_start_failure = 0;
    stop_after                                                       = 2;
    start_failure = thread_delete_failure = 0;
    sema_delete_failure                   = -1;
    detect_hook                           = NULL;
    step_hook                             = NULL;
    sleep_hook                            = NULL;

    memset(semaphores, 0, sizeof(semaphores));
    TEST_ASSERT_TRUE(cd_open(1) == 0);
}

void tearDown(void)
{
    status_failure = start_failure = thread_delete_failure = 0;
    sema_delete_failure                                    = -1;

    TEST_ASSERT_EQUAL_INT(0, cd_close());
}

static void toc_script(CdController* c)
{
    c->status = (CdPlaybackStatus){ .present = !sleeps, .tracks = 2, .generation = sleeps ? 2 : 1 };

    callbacks.stage(&c->status, "Disc identity");

    CdToc toc;

    TEST_ASSERT_TRUE(!(cd_copy_toc(c->status.generation, &toc) == 0));

    c->toc = (CdToc){ .count = 2, .start = { 0, 100, 200 } };
}

static void read_toc_after_publication(void)
{
    CdToc toc;

    if (sleeps == 1)
    {
        TEST_ASSERT_TRUE(cd_copy_toc(1, &toc) == 0);
        TEST_ASSERT_EQUAL_INT(100, toc.start[1]);
        TEST_ASSERT_TRUE(!(cd_copy_toc(2, &toc) == 0));
    }
    else
    {
        TEST_ASSERT_TRUE(!(cd_copy_toc(2, &toc) == 0));
    }
}

static void lookup_toc_is_generation_scoped(void)
{
    detect_hook = toc_script;
    sleep_hook  = read_toc_after_publication;

    test_worker_run();
}

static void autoplay_waits_for_source_handoff(void)
{
    test_worker_run();
    TEST_ASSERT_EQUAL_INT(2, sleeps);
    TEST_ASSERT_EQUAL_INT(1, steps);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
}

static void transport_script(CdController* c)
{
    c->status = (CdPlaybackStatus){ .present = 1, .generation = 1 };

    callbacks.stage(&c->status, "Ready");

    if (!sleeps)
    {
        const AudioTransportRequests requests = { .commands = { AUDIO_TRANSPORT_STOP, AUDIO_TRANSPORT_NEXT }, .command_count = 2, .program = { 3, 1, 2 }, .program_count = 3, .scan_direction = -1 };

        cd_transport_apply(1, &requests);

        const AudioTransportRequests release = { 0 };

        cd_transport_apply(1, &release);

        detect_hook = NULL;
    }
}

static void inspect_requests(CdController* c, const CdRequests* requests)
{
    if (steps == 1)
    {
        TEST_ASSERT_TRUE(requests->have_command);
        TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_STOP, requests->command);
        TEST_ASSERT_EQUAL_UINT(3, requests->program_count);

        const int program[] = { 3, 1, 2 };

        TEST_ASSERT_EQUAL_INT_ARRAY(program, requests->program, 3);
        TEST_ASSERT_EQUAL_INT(0, requests->scan_direction);
    }
    else if (steps == 2)
    {
        TEST_ASSERT_TRUE(requests->have_command);
        TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_NEXT, requests->command);
        TEST_ASSERT_EQUAL_UINT(0, requests->program_count);

        c->status.generation = 2;

        callbacks.stage(&c->status, "Replacement");
        cd_transport_command(1, AUDIO_TRANSPORT_NEXT);
    }
    else
    {
        TEST_ASSERT_FALSE(requests->have_command);
    }
}

static void transport_batch_preserves_order_and_generation(void)
{
    detect_hook = transport_script;
    step_hook   = inspect_requests;
    stop_after  = 4;

    test_worker_run();
    TEST_ASSERT_EQUAL_INT(3, steps);
}

static void failed_join_rejects_reopen(void)
{
    test_worker_run();

    status_failure = 1;

    TEST_ASSERT_EQUAL_INT(CD_ERROR_WORKER_CLOSE, cd_close());
    TEST_ASSERT_TRUE(!(cd_open(0) == 0));
    TEST_ASSERT_EQUAL_UINT(1, test_worker_opens());
    TEST_ASSERT_EQUAL_INT(2, next_semaphore);

    status_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_close());

    next_semaphore = 0;

    TEST_ASSERT_TRUE(cd_open(0) == 0);

    CdPlaybackStatus status;

    cd_poll(&status);
    TEST_ASSERT_EQUAL_UINT(0, status.generation);
    test_worker_run();
    TEST_ASSERT_EQUAL_INT(0, configured_autoplay);
}

static void failed_drive_start_reports_unavailability_until_reopen(void)
{
    CdPlaybackStatus status;

    TEST_ASSERT_TRUE(cd_poll(&status) == 0);
    TEST_ASSERT_TRUE(status.checking);

    drive_start_failure = 1;

    test_worker_run();
    TEST_ASSERT_TRUE(!(cd_poll(&status) == 0));
    TEST_ASSERT_FALSE(status.checking);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE INIT FAILED", status.error);
    TEST_ASSERT_EQUAL_INT(0, steps);

    status_failure = 1;

    TEST_ASSERT_EQUAL_INT(CD_ERROR_WORKER_CLOSE, cd_close());
    TEST_ASSERT_TRUE(!(cd_open(0) == 0));
    TEST_ASSERT_TRUE(!(cd_poll(&status) == 0));
    TEST_ASSERT_EQUAL_STRING("CD DRIVE INIT FAILED", status.error);

    status_failure = drive_start_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_close());

    next_semaphore = 0;

    TEST_ASSERT_TRUE(cd_open(1) == 0);
    TEST_ASSERT_TRUE(cd_poll(&status) == 0);
    TEST_ASSERT_TRUE(status.checking);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    test_worker_run();
    TEST_ASSERT_EQUAL_INT(1, steps);
}

static void runtime_drive_failure(CdController* c, const CdRequests* requests)
{
    (void)requests;

    c->status.restart_required = 1;

    strcpy(c->status.error, "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
}

static void poll_runtime_drive_failure(void)
{
    CdPlaybackStatus status;

    TEST_ASSERT_EQUAL_INT(CD_ERROR_RESTART_REQUIRED, cd_poll(&status));
    TEST_ASSERT_TRUE(status.restart_required);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", status.error);
}

static void restart_required_is_reported_while_worker_is_running(void)
{
    selected   = 1;
    step_hook  = runtime_drive_failure;
    sleep_hook = poll_runtime_drive_failure;

    test_worker_run();
    TEST_ASSERT_EQUAL_INT(2, sleeps);
}

static void restart_required_survives_worker_exit_and_pending_cleanup(void)
{
    CdPlaybackStatus status;

    drive_restart_required = 1;

    test_worker_run();
    TEST_ASSERT_EQUAL_INT(CD_ERROR_RESTART_REQUIRED, cd_poll(&status));
    TEST_ASSERT_TRUE(status.restart_required);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", status.error);
    TEST_ASSERT_EQUAL_INT(0, steps);

    status_failure = 1;

    TEST_ASSERT_EQUAL_INT(CD_ERROR_WORKER_CLOSE, cd_close());
    TEST_ASSERT_EQUAL_INT(CD_ERROR_RESTART_REQUIRED, cd_poll(&status));
    TEST_ASSERT_TRUE(status.restart_required);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", status.error);
}

static void failed_start_reports_error_until_cleanup(void)
{
    TEST_ASSERT_EQUAL_INT(0, cd_close());

    next_semaphore = 0;
    start_failure = thread_delete_failure = 1;

    TEST_ASSERT_TRUE(!(cd_open(1) == 0));

    Audio            audio;
    CdPlaybackStatus status;

    cd_poll(&status);
    TEST_ASSERT_FALSE(status.checking);
    TEST_ASSERT_EQUAL_STRING("CD WORKER START FAILED", status.error);
    memset(&audio, 0x7f, sizeof(audio));
    cd_copy_snapshot(&status, &audio);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
    TEST_ASSERT_EQUAL_INT(CD_ERROR_WORKER_CLOSE, cd_close());
    TEST_ASSERT_TRUE(!(cd_open(0) == 0));
    TEST_ASSERT_EQUAL_INT(2, next_semaphore);

    thread_delete_failure = 0;
    sema_delete_failure   = 1;

    TEST_ASSERT_EQUAL_INT(CD_ERROR_WORKER_CLOSE, cd_close());
    cd_poll(&status);
    TEST_ASSERT_FALSE(status.checking);
    TEST_ASSERT_EQUAL_STRING("CD WORKER START FAILED", status.error);
    TEST_ASSERT_TRUE(!(cd_open(0) == 0));

    sema_delete_failure = -1;

    TEST_ASSERT_EQUAL_INT(0, cd_close());

    next_semaphore = 0;
    start_failure  = 0;

    TEST_ASSERT_TRUE(cd_open(0) == 0);
    cd_poll(&status);
    TEST_ASSERT_TRUE(status.checking);
    TEST_ASSERT_EQUAL_STRING("", status.error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(transport_batch_preserves_order_and_generation);
    RUN_TEST(autoplay_waits_for_source_handoff);
    RUN_TEST(replacement_publication);
    RUN_TEST(replacement_between_reads);
    RUN_TEST(unavailable_snapshot);
    RUN_TEST(lookup_toc_is_generation_scoped);
    RUN_TEST(failed_join_rejects_reopen);
    RUN_TEST(failed_start_reports_error_until_cleanup);
    RUN_TEST(failed_drive_start_reports_unavailability_until_reopen);
    RUN_TEST(restart_required_is_reported_while_worker_is_running);
    RUN_TEST(restart_required_survives_worker_exit_and_pending_cleanup);

    return UNITY_END();
}

int ReferThreadStatus(int id, ee_thread_status_t* status)
{
    (void)id;

    if (status_failure)
    {
        return -1;
    }

    status->status = THS_DORMANT;

    return 0;
}

void platform_sleep_us(uint32_t microseconds)
{
    (void)__wrap_usleep(microseconds);
}
