#include "platform/memorycard/card.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include "unity.h"
#include <libmc.h>
#include <string.h>

static int modules, probes, delays, initializations, infos;
static int fail_module, rpc_ready_after, init_result, queue_result, sync_result;
static int card_result, card_type, card_formatted, changed_card, expected_port;

/**
 * @brief Substitute platform_iop_module in this host fixture.
 */
int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    static const char* names[] = { "sio2man", "mcman", "mcserv" };
    static const char* paths[] = { "rom0:SIO2MAN", "rom0:MCMAN", "rom0:MCSERV" };

    TEST_ASSERT_LESS_THAN_INT(3, modules);
    TEST_ASSERT_EQUAL_STRING(names[modules], module->name);
    TEST_ASSERT_EQUAL_STRING(paths[modules], module->path);
    TEST_ASSERT_NULL(error);
    TEST_ASSERT_EQUAL_UINT(0, capacity);
    ++modules;

    return modules == fail_module ? -1 : 0;
}

/**
 * @brief Substitute platform_iop_probe in this host fixture.
 */
int platform_iop_probe(unsigned id)
{
    TEST_ASSERT_EQUAL_INT(3, modules);
    TEST_ASSERT_EQUAL_HEX32(PS2_RPC_MEMORYCARD, id);
    ++probes;

    return probes >= rpc_ready_after;
}

/**
 * @brief Substitute card_test_usleep in this host fixture.
 */
int card_test_usleep(unsigned microseconds)
{
    TEST_ASSERT_EQUAL_UINT(PS2_RPC_PROBE_DELAY_US, microseconds);
    ++delays;

    return 0;
}

/**
 * @brief Substitute mcInit in this host fixture.
 */
int mcInit(int type)
{
    TEST_ASSERT_EQUAL_INT(MC_TYPE_MC, type);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(rpc_ready_after, probes);
    ++initializations;

    return init_result;
}

/**
 * @brief Substitute mcGetInfo in this host fixture.
 */
int mcGetInfo(int port, int slot, int* type, int* free_space, int* formatted)
{
    TEST_ASSERT_EQUAL_INT(1, initializations);
    TEST_ASSERT_EQUAL_INT(expected_port, port);
    TEST_ASSERT_EQUAL_INT(0, slot);

    *type       = card_type;
    *free_space = 1024;
    *formatted  = card_formatted;

    ++infos;

    return queue_result;
}

/**
 * @brief Substitute mcSync in this host fixture.
 */
int mcSync(int mode, int* command, int* result)
{
    TEST_ASSERT_EQUAL_INT(MC_WAIT, mode);
    TEST_ASSERT_NULL(command);

    *result = infos == 1 && changed_card ? sceMcResChangedCard : card_result;

    return sync_result;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    modules         = 0;
    probes          = 0;
    delays          = 0;
    initializations = 0;
    infos           = 0;
    fail_module     = 0;
    rpc_ready_after = 1;
    init_result     = 0;
    queue_result    = 0;
    sync_result     = 1;
    card_result     = 0;
    card_type       = MC_TYPE_PS2;
    card_formatted  = MC_FORMATTED;
    changed_card    = 0;
    expected_port   = 0;
}

/**
 * @brief No per-test cleanup is required.
 */
void tearDown(void)
{}

/**
 * @brief Verify cold card startup.
 */
static void cold_card_startup(void)
{
    rpc_ready_after = 3;
    changed_card    = 1;

    TEST_ASSERT_EQUAL_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(3, modules);
    TEST_ASSERT_EQUAL_INT(2, delays);
    TEST_ASSERT_EQUAL_INT(2, infos);
}

/**
 * @brief Verify slot two and already initialized libmc.
 */
static void slot_two_and_already_initialized_libmc(void)
{
    expected_port = 1;
    init_result   = -1;

    TEST_ASSERT_EQUAL_INT(0, platform_memorycard_open("mc1:/BOOT/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(1, infos);
}

/**
 * @brief Verify other devices do not load card services.
 */
static void other_devices_do_not_load_card_services(void)
{
    TEST_ASSERT_EQUAL_INT(0, platform_memorycard_open("host:stroom.elf"));
    TEST_ASSERT_EQUAL_INT(0, platform_memorycard_open("mass:/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(0, modules);
    TEST_ASSERT_EQUAL_INT(0, probes);
    TEST_ASSERT_EQUAL_INT(0, initializations);
}

/**
 * @brief Verify missing rpc is bounded.
 */
static void missing_rpc_is_bounded(void)
{
    rpc_ready_after = PS2_RPC_PROBE_ATTEMPTS + 1;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(PS2_RPC_PROBE_ATTEMPTS, probes);
    TEST_ASSERT_EQUAL_INT(PS2_RPC_PROBE_ATTEMPTS, delays);
    TEST_ASSERT_EQUAL_INT(0, initializations);
}

/**
 * @brief Verify failed modules stop before binding.
 */
static void failed_modules_stop_before_binding(void)
{
    fail_module = 2;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(2, modules);
    TEST_ASSERT_EQUAL_INT(0, probes);
}

/**
 * @brief Verify failed library initialization stops before card commands.
 */
static void failed_library_initialization_stops_before_card_commands(void)
{
    init_result = -120;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    TEST_ASSERT_EQUAL_INT(0, infos);
}

/**
 * @brief Verify absent unformatted and failed cards are rejected.
 */
static void absent_unformatted_and_failed_cards_are_rejected(void)
{
    card_type = 0;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    setUp();

    card_formatted = 0;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    setUp();

    card_result = -4;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    setUp();

    queue_result = -1;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
    setUp();

    sync_result = -1;

    TEST_ASSERT_LESS_THAN_INT(0, platform_memorycard_open("mc0:/BOOT/BOOT.ELF"));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(cold_card_startup);
    RUN_TEST(slot_two_and_already_initialized_libmc);
    RUN_TEST(other_devices_do_not_load_card_services);
    RUN_TEST(missing_rpc_is_bounded);
    RUN_TEST(failed_modules_stop_before_binding);
    RUN_TEST(failed_library_initialization_stops_before_card_commands);
    RUN_TEST(absent_unformatted_and_failed_cards_are_rejected);

    return UNITY_END();
}
