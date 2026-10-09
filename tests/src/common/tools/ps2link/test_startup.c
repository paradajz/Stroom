#include "sdk.h"
#include "platform/memorycard/card.h"
#include "unity.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFINE_MODULE(name)      \
    unsigned char name##_irx[1]; \
    unsigned int  size_##name##_irx = 1;

LAUNCHER_MODULES(DEFINE_MODULE)

static int         resets, syncs, modules, configured, command_ready, card_calls, deleted;
static const char* expected_executable;

/**
 * @brief Substitute ps2link_main in this host fixture.
 */
int ps2link_main(int argc, char** argv);

/**
 * @brief Substitute __start in this host fixture.
 */
void __start(void)
{}

/**
 * @brief Substitute SifInitRpc in this host fixture.
 */
void SifInitRpc(int mode)
{
    TEST_ASSERT_EQUAL_INT(0, mode);
}

/**
 * @brief Substitute init_scr in this host fixture.
 */
void init_scr(void)
{}

/**
 * @brief Substitute scr_printf in this host fixture.
 */
int scr_printf(const char* format, ...)
{
    (void)format;

    return 0;
}

/**
 * @brief Substitute platform_memorycard_open in this host fixture.
 */
int platform_memorycard_open(const char* executable)
{
    TEST_ASSERT_EQUAL_STRING(expected_executable, executable);
    TEST_ASSERT_EQUAL_INT(0, resets);
    ++card_calls;

    return getenv("CARD_FAIL") ? -1 : 0;
}

/**
 * @brief Substitute installExceptionHandlers in this host fixture.
 */
void installExceptionHandlers(void)
{
    TEST_ASSERT_EQUAL_INT(1, card_calls);
}

/**
 * @brief Substitute SifIopReset in this host fixture.
 */
int SifIopReset(const char* command, int mode)
{
    TEST_ASSERT_NULL(command);
    TEST_ASSERT_EQUAL_INT(0, mode);
    TEST_ASSERT_EQUAL_INT(0, modules);
    ++resets;

    return resets == 2;
}

/**
 * @brief Substitute SifIopSync in this host fixture.
 */
int SifIopSync(void)
{
    TEST_ASSERT_EQUAL_INT(2, resets);
    ++syncs;

    return syncs == 2;
}

/**
 * @brief Substitute sbv_patch_enable_lmb in this host fixture.
 */
int sbv_patch_enable_lmb(void)
{
    TEST_ASSERT_EQUAL_INT(2, syncs);

    return 0;
}

/**
 * @brief Substitute sbv_patch_disable_prefix_check in this host fixture.
 */
int sbv_patch_disable_prefix_check(void)
{
    TEST_ASSERT_EQUAL_INT(2, syncs);

    return 0;
}

/**
 * @brief Substitute SifExecModuleBuffer in this host fixture.
 */
int SifExecModuleBuffer(void* data, unsigned size, unsigned args_size, const char* args, int* result)
{
    TEST_ASSERT_EQUAL_INT(2, syncs);
    TEST_ASSERT_EQUAL_UINT(1, size);
    ++modules;

    *result = 0;

    if (data == ps2ip_nm_irx)
    {
        const char* names[]  = { "EXPECTED_IP", "EXPECTED_NETMASK", "EXPECTED_GATEWAY" };
        unsigned    consumed = 0;

        for (unsigned i = 0; i < 3; ++i)
        {
            TEST_ASSERT_LESS_THAN_UINT(args_size, consumed);
            TEST_ASSERT_EQUAL_STRING(getenv(names[i]), args + consumed);

            consumed += strlen(args + consumed) + 1;
        }

        TEST_ASSERT_EQUAL_UINT(consumed, args_size);
        ++configured;
    }

    return 1;
}

/**
 * @brief Substitute initCmdRpc in this host fixture.
 */
int initCmdRpc(void)
{
    TEST_ASSERT_EQUAL_INT(8, modules);
    TEST_ASSERT_EQUAL_INT(1, configured);
    ++command_ready;

    return 0;
}

/**
 * @brief Substitute GetMemorySize in this host fixture.
 */
int GetMemorySize(void)
{
    return 32 * 1024 * 1024;
}

/**
 * @brief Substitute ExitDeleteThread in this host fixture.
 */
void ExitDeleteThread(void)
{
    TEST_ASSERT_EQUAL_INT(1, command_ready);
    ++deleted;
}

/**
 * @brief No per-test setup is required.
 */
void setUp(void)
{}

/**
 * @brief No per-test cleanup is required.
 */
void tearDown(void)
{}

/**
 * @brief Verify launch.
 */
static void launch(void)
{
    const char* scenario = getenv("LAUNCH_PATH");
    int         result;

    if (!scenario)
    {
        TEST_FAIL_MESSAGE("LAUNCH_PATH must select a startup scenario");

        return;
    }

    expected_executable = "mc0:/BOOT/BOOT.ELF";

    if (!strcmp(scenario, "missing"))
    {
        result = ps2link_main(0, NULL);
    }
    else if (!strcmp(scenario, "null"))
    {
        char* argv[] = { NULL };

        result = ps2link_main(1, argv);
    }
    else if (!strcmp(scenario, "empty"))
    {
        char* argv[] = { "" };

        result = ps2link_main(1, argv);
    }
    else
    {
        char* argv[] = { (char*)scenario };

        expected_executable = scenario;
        result              = ps2link_main(1, argv);
    }

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(1, deleted);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(launch);

    return UNITY_END();
}
