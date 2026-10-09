#include "ui/cd_player/controller.h"
#include "ui/cd_player/navigation.h"
#include <string.h>

int player_track_programmed(const PlayerState* player, int track)
{
    if (track < 1 || track > CD_MAX_TRACKS)
    {
        return 0;
    }

    for (int i = 0; i < player->count; ++i)
    {
        if (player->tracks[i] == track)
        {
            return 1;
        }
    }

    return 0;
}

void player_init(PlayerState* player)
{
    *player = (PlayerState){ 0 };
}

void player_begin_program(PlayerState* player, const CdPlaybackStatus* cd)
{
    player->editing = 1;
    player->count   = player->saved_count;

    memcpy(player->tracks, player->saved_tracks, sizeof(player->tracks));

    int track = cd->track ? cd->track : 1;

    player->page               = player_page_index(track);
    player->slot               = track - player_page(cd->tracks, player->page).first;
    player->shoulder.direction = 0;
}

/**
 * @brief Append one playback command to the frame effects.
 *
 * @param a Effects record with room for another command.
 * @param command Command to append.
 */
static void emit(AudioTransportRequests* a, AudioTransportCommand command)
{
    a->commands[a->command_count++] = command;
}

/**
 * @brief Translate a shoulder tap into a track skip or a hold into scanning.
 *
 * Taps commit on release; holds scan after the threshold and never skip on release. Only fresh press edges start gestures.
 *
 * @param p Gesture timing state.
 * @param a Destination frame effects.
 * @param pressed Newly pressed shoulder bits.
 * @param held Held shoulder bits.
 * @param now Current monotonic time in milliseconds.
 */
static void gesture(PlayerGesture* p, AudioTransportRequests* a, unsigned pressed, unsigned held, uint32_t now)
{
    unsigned buttons = held & (INPUT_L1 | INPUT_R1);

    if (buttons == (INPUT_L1 | INPUT_R1))
    {
        p->direction = 0;

        return;
    }

    int direction = buttons == INPUT_R1 ? 1 : buttons == INPUT_L1 ? -1
                                                                  : 0;

    if (p->direction && direction != p->direction)
    {
        if (!buttons && (uint32_t)(now - p->started) < PLAYER_SCAN_HOLD_MS)
        {
            emit(a, p->direction > 0 ? AUDIO_TRANSPORT_NEXT : AUDIO_TRANSPORT_PREVIOUS);
        }

        p->direction = 0;
    }

    if (direction && (pressed & buttons))
    {
        p->direction = direction;
        p->started   = now;
    }

    if (p->direction && (uint32_t)(now - p->started) >= PLAYER_SCAN_HOLD_MS)
    {
        a->scan_direction = p->direction;
    }
}

/**
 * @brief Confirm or cancel program editing; an empty program cannot be confirmed.
 *
 * @param p Player draft and focus to update.
 * @param a Destination program effects.
 * @param confirm Nonzero to confirm; zero to cancel.
 */
static void finish(PlayerState* p, AudioTransportRequests* a, int confirm)
{
    if (confirm && !p->count)
    {
        return;
    }

    if (confirm)
    {
        p->saved_count = p->count;

        memcpy(p->saved_tracks, p->tracks, sizeof(p->tracks));

        a->program_count = p->count;

        memcpy(a->program, p->tracks, sizeof(int) * p->count);
    }

    p->editing = 0;
}

AudioTransportRequests player_update(PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed, unsigned held, int active, uint32_t now_ms)
{
    AudioTransportRequests a = { 0 };

    if (!active)
    {
        player->shoulder.direction = 0;

        return a;
    }

    PlayerPage page = player_page(cd->tracks, player->page);

    player->page = page.count ? player_page_index(page.first) : 0;

    if (player->slot >= page.slots)
    {
        player->slot = page.slots ? page.slots - 1 : 0;
    }

    if (player->editing && (pressed & (INPUT_SQUARE | INPUT_TRIANGLE | INPUT_SELECT)))
    {
        player->shoulder.direction = 0;

        finish(player, &a, !!(pressed & INPUT_SQUARE) && !(pressed & (INPUT_TRIANGLE | INPUT_SELECT)));
        return a;
    }

    if (pressed & INPUT_START)
    {
        player->shoulder.direction = 0;

        emit(&a, AUDIO_TRANSPORT_PLAY_PAUSE);
        return a;
    }

    if (pressed & INPUT_TRIANGLE)
    {
        emit(&a, AUDIO_TRANSPORT_STOP);

        player->shoulder.direction = 0;

        return a;
    }

    player_navigate(player, cd, pressed);

    page = player_page(cd->tracks, player->page);

    if (player->editing && (pressed & INPUT_CROSS))
    {
        int track = player_page_track(page, player->slot);

        if (track < 0)
        {
            player_page_turn(player, cd->tracks);
        }
        else if (track > 0)
        {
            int index = -1;

            for (int i = 0; i < player->count; ++i)
            {
                if (player->tracks[i] == track)
                {
                    index = i;
                }
            }

            if (index >= 0)
            {
                memmove(player->tracks + index, player->tracks + index + 1, (player->count - index - 1) * sizeof(*player->tracks));
                --player->count;
            }
            else if (player->count < CD_MAX_TRACKS)
            {
                player->tracks[player->count++] = track;
            }
        }
    }

    if (!player->editing)
    {
        gesture(&player->shoulder, &a, pressed, held, now_ms);
    }

    if (player->editing)
    {
        player->shoulder.direction = 0;
    }

    return a;
}
