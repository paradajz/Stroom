#include "util/random.h"
#include "audio/cd/cd_playback.h"

#define SHUFFLE_FALLBACK_SEED 0x7192ace3u

/**
 * @brief Advance the playback xorshift generator, seeding zero state.
 *
 * @param p Playback RNG state to update.
 * @return Next 32-bit random value.
 */
static uint32_t random_next(CdPlayback* p)
{
    if (!p->random)
    {
        p->random = SHUFFLE_FALLBACK_SEED;
    }

    return p->random = util_random_next_u32(p->random);
}

void cd_playback_mode(CdPlayback* p, CdPlaybackMode mode, int tracks, int current)
{
    if (mode == CD_PROGRAM && !p->program_count)
    {
        return;
    }

    p->repeat = CD_REPEAT_OFF;

    if (mode == CD_PROGRAM)
    {
        p->mode  = mode;
        p->count = p->program_count;

        for (int i = 0; i < p->count; ++i)
        {
            p->order[i] = p->program[i];
        }

        return;
    }

    p->mode  = mode;
    p->count = tracks;

    for (int i = 0; i < tracks; ++i)
    {
        p->order[i] = i + 1;
    }

    if (mode == CD_SHUFFLE && tracks > 0)
    {
        for (int i = tracks - 1; i > 0; --i)
        {
            int j = random_next(p) % (i + 1), temp = p->order[i];

            p->order[i] = p->order[j];
            p->order[j] = temp;
        }

        for (int i = 0; i < tracks; ++i)
        {
            if (p->order[i] == current)
            {
                int temp = p->order[0];

                p->order[0] = current;
                p->order[i] = temp;

                break;
            }
        }
    }
}

int cd_playback_program(CdPlayback* p, const int* tracks, unsigned count, int total)
{
    if (!count || count > CD_MAX_TRACKS)
    {
        return CD_PLAYBACK_ERROR_PROGRAM_SIZE;
    }

    for (unsigned i = 0; i < count; ++i)
    {
        if (tracks[i] < 1 || tracks[i] > total)
        {
            return CD_PLAYBACK_ERROR_TRACK_RANGE;
        }

        /* Unique entries keep selection/scan reconciliation unambiguous. */

        for (unsigned j = 0; j < i; ++j)
        {
            if (tracks[j] == tracks[i])
            {
                return CD_PLAYBACK_ERROR_DUPLICATE_TRACK;
            }
        }
    }

    p->program_count = count;
    p->repeat        = CD_REPEAT_OFF;
    p->mode          = CD_PROGRAM;
    p->count         = count;

    for (unsigned i = 0; i < count; ++i)
    {
        p->program[i] = p->order[i] = tracks[i];
    }

    return 0;
}

int cd_playback_next(CdPlayback* p, int current, int direction, int automatic)
{
    if (!p->count)
    {
        return 0;
    }

    if (automatic && p->repeat == CD_REPEAT_ONE)
    {
        return current;
    }

    int index = -1;

    for (int i = 0; i < p->count; ++i)
    {
        if (p->order[i] == current)
        {
            index = i;

            break;
        }
    }

    int next = index + (direction < 0 ? -1 : 1);

    if (index < 0)
    {
        next = 0;
    }

    if (next < 0 || next >= p->count)
    {
        if (p->repeat == CD_REPEAT_ALL)
        {
            next = next < 0 ? p->count - 1 : 0;
        }
        else
        {
            return automatic ? 0 : current;
        }
    }

    return p->order[next];
}
