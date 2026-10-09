#include "platform/boot/path.h"
#include "unity.h"

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
 * @brief Verify dev1 fallback for missing arguments.
 */
static void dev1_fallback_for_missing_arguments(void)
{
    char* empty[]   = { "" };
    char* missing[] = { NULL };

    TEST_ASSERT_EQUAL_STRING("mc0:/BOOT/BOOT.ELF", platform_boot_executable(0, NULL));
    TEST_ASSERT_EQUAL_STRING("mc0:/BOOT/BOOT.ELF", platform_boot_executable(1, NULL));
    TEST_ASSERT_EQUAL_STRING("mc0:/BOOT/BOOT.ELF", platform_boot_executable(1, empty));
    TEST_ASSERT_EQUAL_STRING("mc0:/BOOT/BOOT.ELF", platform_boot_executable(1, missing));
}

/**
 * @brief Verify settings follow the executable device and directory.
 */
static void settings_follow_the_executable_device_and_directory(void)
{
    const char* paths[]    = { "mc0:/BOOT/BOOT.ELF", "mc1:/BOOT/BOOT.ELF", "mass:/apps/PS2LINK.ELF", "host:stroom.elf" };
    const char* expected[] = { "mc0:/BOOT/IPCONFIG.DAT", "mc1:/BOOT/IPCONFIG.DAT", "mass:/apps/IPCONFIG.DAT", "host:IPCONFIG.DAT" };
    char        path[128];

    for (unsigned i = 0; i < sizeof(paths) / sizeof(*paths); ++i)
    {
        char* argv[] = { (char*)paths[i] };

        TEST_ASSERT_EQUAL_STRING(paths[i], platform_boot_executable(1, argv));
        TEST_ASSERT_EQUAL_INT(0, platform_boot_config_path(path, sizeof(path), paths[i], "IPCONFIG.DAT"));
        TEST_ASSERT_EQUAL_STRING(expected[i], path);
    }
}

/**
 * @brief Verify undersized destination is not modified.
 */
static void undersized_destination_is_not_modified(void)
{
    char path[8] = "guard";

    TEST_ASSERT_EQUAL_INT(-1, platform_boot_config_path(path, sizeof(path), "mc0:/BOOT/BOOT.ELF", "STROOM.DAT"));
    TEST_ASSERT_EQUAL_STRING("guard", path);
    TEST_ASSERT_EQUAL_INT(-1, platform_boot_config_path(path, 0, "host:stroom.elf", "STROOM.DAT"));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(dev1_fallback_for_missing_arguments);
    RUN_TEST(settings_follow_the_executable_device_and_directory);
    RUN_TEST(undersized_destination_is_not_modified);

    return UNITY_END();
}
