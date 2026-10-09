#include "ui/cd_player/controller.h"
#include "ui/cd_player/navigation.h"
#include "unity.h"
#include <string.h>

void setUp(void)
{}

void tearDown(void)
{}

static AudioTransportRequests press(PlayerState* player, const CdPlaybackStatus* cd, unsigned input)
{
    return player_update(player, cd, input, 0, 1, 0);
}

/** Every 1..99-track disc is reachable through one-row scrolling and a final ellipsis. */
static void all_tracks_reachable(void)
{
    for (int total = 1; total <= CD_MAX_TRACKS; ++total)
    {
        unsigned char seen[CD_MAX_TRACKS] = { 0 };
        PlayerState   player;

        player_init(&player);

        for (int step = 0; step < 20; ++step)
        {
            PlayerPage page = player_page(total, player.page);

            TEST_ASSERT_LESS_OR_EQUAL_INT(16, page.slots);

            for (int slot = 0; slot < page.slots; ++slot)
            {
                int track = player_page_track(page, slot);

                if (track > 0)
                {
                    TEST_ASSERT_LESS_OR_EQUAL_INT(total, track);

                    seen[track - 1] = 1;
                }
            }

            if (!page.next)
            {
                break;
            }

            TEST_ASSERT_EQUAL_INT(PLAYER_PAGE_NEXT, player_page_track(page, 15));
            player_page_turn(&player, total);

            if (!player.page)
            {
                break;
            }
        }

        for (int i = 0; i < total; ++i)
        {
            TEST_ASSERT_TRUE(seen[i]);
        }

        TEST_ASSERT_EQUAL_INT(0, player.page);
    }

    TEST_ASSERT_EQUAL_INT(9, player_page(99, 1).first);
    TEST_ASSERT_EQUAL_INT(89, player_page(99, 99).first);
    TEST_ASSERT_EQUAL_INT(16, player_page_track(player_page(16, 0), 15));
    TEST_ASSERT_EQUAL_INT(0, player_page_track(player_page(99, 11), 14));
    TEST_ASSERT_EQUAL_INT(0, player_page(0, 0).slots);
}

static void programming_and_navigation(void)
{
    CdPlaybackStatus cd = { .tracks = 17, .track = 1 };
    PlayerState      player;

    player_init(&player);
    player_begin_program(&player, &cd);
    TEST_ASSERT_TRUE(player.editing);

    AudioTransportRequests actions = press(&player, &cd, INPUT_CROSS);

    TEST_ASSERT_EQUAL_UINT(0, actions.command_count);
    TEST_ASSERT_EQUAL_INT(1, player.tracks[0]);
    press(&player, &cd, INPUT_DOWN);
    TEST_ASSERT_EQUAL_INT(8, player.slot);
    press(&player, &cd, INPUT_UP);
    TEST_ASSERT_EQUAL_INT(0, player.slot);

    player.slot = 15;

    press(&player, &cd, INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(1, player.page);
    TEST_ASSERT_EQUAL_INT(0, player.slot);

    player.slot = 15;

    press(&player, &cd, INPUT_LEFT);
    TEST_ASSERT_EQUAL_INT(8, player.slot);
    press(&player, &cd, INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(17, player.tracks[1]);

    actions = press(&player, &cd, INPUT_SQUARE);

    TEST_ASSERT_EQUAL_UINT(2, actions.program_count);
    TEST_ASSERT_EQUAL_INT(17, actions.program[1]);
    TEST_ASSERT_FALSE(player.editing);
    player_begin_program(&player, &cd);
    press(&player, &cd, INPUT_TRIANGLE);
    TEST_ASSERT_FALSE(player.editing);
    press(&player, &cd, 0);
}

static void shared_transport(void)
{
    CdPlaybackStatus cd = { .tracks = 99 };
    PlayerState      player;

    player_init(&player);

    AudioTransportRequests actions = press(&player, &cd, INPUT_START);

    TEST_ASSERT_EQUAL_UINT(1, actions.command_count);
    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_PLAY_PAUSE, actions.commands[0]);
    player_update(&player, &cd, INPUT_R1, INPUT_R1, 1, 10);

    actions = player_update(&player, &cd, 0, INPUT_R1, 1, 310);

    TEST_ASSERT_EQUAL_INT(1, actions.scan_direction);

    actions = player_update(&player, &cd, 0, 0, 0, 320);

    TEST_ASSERT_EQUAL_INT(0, actions.scan_direction);

    actions = press(&player, &cd, INPUT_TRIANGLE);

    TEST_ASSERT_EQUAL_UINT(1, actions.command_count);
    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_STOP, actions.commands[0]);
    player_update(&player, &cd, INPUT_R1, INPUT_R1, 1, 1000);

    actions = player_update(&player, &cd, 0, INPUT_R1, 1, 1300);

    TEST_ASSERT_EQUAL_INT(1, actions.scan_direction);

    actions = player_update(&player, &cd, INPUT_TRIANGLE, INPUT_R1, 1, 1320);

    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_STOP, actions.commands[0]);
    TEST_ASSERT_EQUAL_INT(0, actions.scan_direction);
    TEST_ASSERT_EQUAL_INT(0, player.shoulder.direction);

    actions = player_update(&player, &cd, 0, 0, 1, 1340);

    TEST_ASSERT_EQUAL_UINT(0, actions.command_count);
}

static void physical_gestures(void)
{
    CdPlaybackStatus       cd = { .tracks = 17 };
    PlayerState            p;
    AudioTransportRequests a;

    for (int direction = -1; direction <= 1; direction += 2)
    {
        unsigned button = direction < 0 ? INPUT_L1 : INPUT_R1;

        player_init(&p);

        a = player_update(&p, &cd, button, button, 1, 100);

        TEST_ASSERT_TRUE_MESSAGE(!a.command_count && !a.scan_direction, "!a.command_count && !a.scan_direction");

        a = player_update(&p, &cd, 0, 0, 1, 200);

        TEST_ASSERT_TRUE_MESSAGE(a.command_count == 1 && a.commands[0] == (direction < 0 ? AUDIO_TRANSPORT_PREVIOUS : AUDIO_TRANSPORT_NEXT), "a.command_count == 1 && a.commands[0] == (direction < 0 ? AUDIO_TRANSPORT_PREVIOUS : AUDIO_TRANSPORT_NEXT)");
        TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction, "!a.scan_direction");

        a = player_update(&p, &cd, 0, 0, 1, 220);

        TEST_ASSERT_TRUE_MESSAGE(!a.command_count, "!a.command_count");
        player_update(&p, &cd, button, button, 1, 300);

        a = player_update(&p, &cd, 0, button, 1, 599);

        TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction, "!a.scan_direction");

        a = player_update(&p, &cd, 0, button, 1, 600);

        TEST_ASSERT_TRUE_MESSAGE(a.scan_direction == direction && !a.command_count, "a.scan_direction == direction && !a.command_count");

        a = player_update(&p, &cd, 0, 0, 1, 700);

        TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction && !a.command_count, "!a.scan_direction && !a.command_count");
    }

    /* Cancellation on settings/view changes suppresses delayed tap events. */
    player_init(&p);
    player_update(&p, &cd, INPUT_R1, INPUT_R1, 1, 100);

    a = player_update(&p, &cd, 0, INPUT_R1, 0, 150);

    TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction && !a.command_count, "!a.scan_direction && !a.command_count");

    a = player_update(&p, &cd, 0, INPUT_R1, 1, 500);

    TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction, "!a.scan_direction");

    a = player_update(&p, &cd, 0, 0, 1, 510);

    TEST_ASSERT_TRUE_MESSAGE(!a.command_count, "!a.command_count");
    /* Both shoulders cancel until a fresh press, even if one releases first. */
    player_update(&p, &cd, INPUT_R1, INPUT_R1, 1, 600);
    player_update(&p, &cd, INPUT_L1, INPUT_L1 | INPUT_R1, 1, 650);

    a = player_update(&p, &cd, 0, INPUT_R1, 1, 1000);

    TEST_ASSERT_TRUE_MESSAGE(!a.scan_direction, "!a.scan_direction");

    a = player_update(&p, &cd, 0, 0, 1, 1010);

    TEST_ASSERT_TRUE_MESSAGE(!a.command_count, "!a.command_count");
    /* Millisecond wraparound and a release at the threshold. */
    player_update(&p, &cd, INPUT_L1, INPUT_L1, 1, UINT32_MAX - 99);

    a = player_update(&p, &cd, 0, INPUT_L1, 1, 200);

    TEST_ASSERT_TRUE_MESSAGE(a.scan_direction == -1, "a.scan_direction == -1");

    a = player_update(&p, &cd, 0, 0, 1, 220);

    TEST_ASSERT_TRUE_MESSAGE(!a.command_count && !a.scan_direction, "!a.command_count && !a.scan_direction");
    player_update(&p, &cd, INPUT_R1, INPUT_R1, 1, 500);

    a = player_update(&p, &cd, 0, 0, 1, 800);

    TEST_ASSERT_TRUE_MESSAGE(!a.command_count, "!a.command_count");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(all_tracks_reachable);
    RUN_TEST(programming_and_navigation);
    RUN_TEST(shared_transport);
    RUN_TEST(physical_gestures);

    return UNITY_END();
}
