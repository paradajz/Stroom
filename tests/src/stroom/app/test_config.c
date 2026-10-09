#include "app/config.h"
#include "unity.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static char directory[64];
static char filename[128];

void setUp(void)
{}

void tearDown(void)
{
    if (filename[0])
    {
        remove(filename);

        filename[0] = 0;
    }

    if (directory[0])
    {
        rmdir(directory);

        directory[0] = 0;
    }
}

static AppConfig parse(const char* text)
{
    AppConfig config = app_config_defaults();
    FILE*     file   = tmpfile();

    TEST_ASSERT_NOT_NULL(file);
    fputs(text, file);
    rewind(file);
    app_config_read(&config, file);
    fclose(file);

    return config;
}

static void frame_rate_preferences(void)
{
    TEST_ASSERT_EQUAL_INT(30, app_config_defaults().frame_rate);
    TEST_ASSERT_EQUAL_INT(60, parse("frame_rate=60\n").frame_rate);
    TEST_ASSERT_EQUAL_INT(30, parse("frame_rate=60\nframe_rate=30\n").frame_rate);
    TEST_ASSERT_EQUAL_INT(60, parse("frame_rate=60\nframe_rate=45\nframe_rate=30fps\n").frame_rate);
}

static void mute_preferences(void)
{
    TEST_ASSERT_FALSE(app_config_defaults().muted);
    TEST_ASSERT_TRUE(parse("mute=on\n").muted);
    TEST_ASSERT_FALSE(parse("mute=on\nmute=off\n").muted);
    TEST_ASSERT_TRUE(parse("mute=on\nmute=invalid\n").muted);
}

static void startup_preferences(void)
{
    AppConfig config = parse("# Startup\r\n display = fullscreen # comment\r\nlevels=off\nmute=on\ncd_autoplay=off\nlookup_host=192.168.1.174\n");

    TEST_ASSERT_FALSE(config.show_panels);
    TEST_ASSERT_FALSE(config.levels);
    TEST_ASSERT_TRUE(config.muted);
    TEST_ASSERT_FALSE(config.cd_autoplay);
    TEST_ASSERT_EQUAL_STRING("192.168.1.174", config.lookup_host);
}

static void invalid_settings_keep_defaults(void)
{
    AppConfig config = parse("display=bad\nlevels=no\nmute=\ncd_autoplay=maybe\nlookup_host=192.168.1.256\nunknown=off\nbroken\n");

    TEST_ASSERT_TRUE(config.show_panels);
    TEST_ASSERT_TRUE(config.levels);
    TEST_ASSERT_FALSE(config.muted);
    TEST_ASSERT_TRUE(config.cd_autoplay);
    TEST_ASSERT_EQUAL_STRING("", config.lookup_host);

    config = app_config_load("/nonexistent/stroom.elf");

    TEST_ASSERT_TRUE(config.cd_autoplay);
    TEST_ASSERT_EQUAL_UINT32(0, config.network.ip);
}

static void static_addresses_are_a_group(void)
{
    AppConfig config = parse("ip=192.168.1.240\nnetmask=255.255.255.0\ngateway=192.168.1.1");

    TEST_ASSERT_EQUAL_HEX32(0xc0a801f0, config.network.ip);
    TEST_ASSERT_EQUAL_HEX32(0xffffff00, config.network.netmask);
    TEST_ASSERT_EQUAL_HEX32(0xc0a80101, config.network.gateway);

    config = parse("ip=192.168.1.240\nnetmask=255.255.255.0\n");

    TEST_ASSERT_EQUAL_UINT32(0, config.network.ip);

    config = parse("ip=192.168.1.240\nnetmask=255.0.255.0\ngateway=192.168.1.1\n");

    TEST_ASSERT_EQUAL_UINT32(0, config.network.ip);
}

static void oversized_lines_are_discarded(void)
{
    char text[600];

    memset(text, ' ', sizeof(text));
    memcpy(text, "mute=on", sizeof("mute=on") - 1);
    strcpy(text + 550, "\nlevels=off\n");

    AppConfig config = parse(text);

    TEST_ASSERT_FALSE(config.muted);
    TEST_ASSERT_FALSE(config.levels);
    memset(text, ' ', sizeof(text));
    memcpy(text, "mute=on", sizeof("mute=on") - 1);

    text[sizeof(text) - 1] = 0;
    config                 = parse(text);

    TEST_ASSERT_FALSE(config.muted);
}

static void loads_beside_executable(void)
{
    strcpy(directory, "/tmp/stroom-config-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(directory));
    snprintf(filename, sizeof(filename), "%s/STROOM.DAT", directory);

    FILE* file = fopen(filename, "w");

    TEST_ASSERT_NOT_NULL(file);
    fputs("cd_autoplay=off\nlookup_host=192.168.1.174\n", file);
    fclose(file);

    char executable[128];

    snprintf(executable, sizeof(executable), "%s/stroom.elf", directory);

    AppConfig config = app_config_load(executable);

    TEST_ASSERT_FALSE(config.cd_autoplay);
    TEST_ASSERT_EQUAL_STRING("192.168.1.174", config.lookup_host);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(loads_beside_executable);
    RUN_TEST(startup_preferences);
    RUN_TEST(frame_rate_preferences);
    RUN_TEST(mute_preferences);
    RUN_TEST(invalid_settings_keep_defaults);
    RUN_TEST(static_addresses_are_a_group);
    RUN_TEST(oversized_lines_are_discarded);

    return UNITY_END();
}
