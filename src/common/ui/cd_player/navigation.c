#include "ui/cd_player/navigation.h"

#define GRID_TRACKS_WITH_LINK (PLAYER_GRID_SLOTS - 1)

PlayerPage player_page(int tracks, int page)
{
    if (tracks < 1)
    {
        return (PlayerPage){ 0 };
    }

    if (tracks > CD_MAX_TRACKS)
    {
        tracks = CD_MAX_TRACKS;
    }

    if (tracks <= PLAYER_GRID_SLOTS)
    {
        return (PlayerPage){ 1, tracks, 0, tracks };
    }

    int last = (tracks - GRID_TRACKS_WITH_LINK + PLAYER_GRID_COLUMNS - 1) / PLAYER_GRID_COLUMNS;

    if (page < 0)
    {
        page = 0;
    }

    if (page > last)
    {
        page = last;
    }

    int first = page * PLAYER_GRID_COLUMNS + 1;
    int count = tracks - first + 1;

    if (count > GRID_TRACKS_WITH_LINK)
    {
        count = GRID_TRACKS_WITH_LINK;
    }

    return (PlayerPage){ first, count, 1, PLAYER_GRID_SLOTS };
}

int player_page_track(PlayerPage page, int slot)
{
    if (slot < 0 || slot >= page.slots)
    {
        return 0;
    }

    if (page.next && slot == PLAYER_GRID_SLOTS - 1)
    {
        return PLAYER_PAGE_NEXT;
    }

    return slot < page.count ? page.first + slot : 0;
}

int player_page_index(int track)
{
    return track > 0 ? (track - 1) / PLAYER_GRID_COLUMNS : 0;
}

void player_page_turn(PlayerState* player, int tracks)
{
    PlayerPage old  = player_page(tracks, player->page);
    int        page = player->page + 1;

    if (old.first + old.count > tracks)
    {
        page = 0;
    }

    PlayerPage next = player_page(tracks, page);

    player->page = next.first == 1 ? 0 : player_page_index(next.first);
    player->slot = 0;
}

void player_navigate(PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed)
{
    if (player->editing)
    {
        PlayerPage page = player_page(cd->tracks, player->page);
        int        slot = player->slot;

        if (pressed & INPUT_LEFT)
        {
            --slot;
        }

        if (pressed & INPUT_RIGHT)
        {
            ++slot;
        }

        if (pressed & INPUT_UP)
        {
            if (slot >= PLAYER_GRID_COLUMNS)
            {
                slot -= PLAYER_GRID_COLUMNS;
            }
            else if (player->page > 0)
            {
                --player->page;

                slot += PLAYER_GRID_COLUMNS;
                page = player_page(cd->tracks, player->page);
            }
        }

        if (pressed & INPUT_DOWN)
        {
            if (slot < PLAYER_GRID_COLUMNS && slot + PLAYER_GRID_COLUMNS < page.slots)
            {
                slot += PLAYER_GRID_COLUMNS;
            }
            else if (page.next)
            {
                int column = slot % PLAYER_GRID_COLUMNS;

                player_page_turn(player, cd->tracks);

                page = player_page(cd->tracks, player->page);
                slot = column;
            }
        }

        if (slot < 0)
        {
            slot = 0;
        }

        if (slot >= page.slots)
        {
            slot = page.slots ? page.slots - 1 : 0;
        }

        if (!player_page_track(page, slot) && page.next)
        {
            slot = (pressed & INPUT_LEFT) ? page.count - 1 : PLAYER_GRID_SLOTS - 1;
        }

        player->slot = slot;

        return;
    }
}
