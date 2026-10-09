#include "platform/cd/drive.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include "platform/time/clock.h"
#include <libcdvd.h>
#include <kernel.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define DRIVE_TOC_BYTES     2064
#define DRIVE_POLL_DELAY_US 1000
#define DRIVE_READ_RETRIES  3

#define READ_SECTORS      16
#define READ_TIMEOUT_MS   5000
#define CANCEL_TIMEOUT_MS 1000

static uint8_t sectors[2][READ_SECTORS * CD_SECTOR_BYTES] __attribute__((aligned(64)));
static uint8_t reserve[PS2_CD_RESERVE_SECTORS][CD_SECTOR_BYTES] __attribute__((aligned(64)));
static uint8_t raw_toc[DRIVE_TOC_BYTES] __attribute__((aligned(64)));
/* Static DMA banks remain quarantined even if the worker is reopened. */
static int dma_quarantined;

int platform_cd_drive_open(Ps2CdDrive* d, const Ps2CdDriveRuntime* runtime)
{
    memset(d, 0, sizeof(*d));

    d->pending = -1;

    if (dma_quarantined)
    {
        d->failed = 1;

        snprintf(runtime->error, runtime->capacity, "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
        return PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;
    }

    runtime->stage(runtime->context, "CHECKING CD DRIVE SERVICES");
    /* Check both resident modules independently: an init RPC alone does not
     * establish that the complete drive service is available. */
    runtime->stage(runtime->context, "LOADING CDVDMAN");

    if (platform_iop_module(&(Ps2IopModule){ .name = "cdvd_driver", .path = "rom0:CDVDMAN" }, NULL, 0) != 0)
    {
        snprintf(runtime->error, runtime->capacity, "CDVDMAN LOAD FAILED");
        return PS2_CD_DRIVE_ERROR_CDVDMAN_LOAD;
    }

    runtime->stage(runtime->context, "LOADING CDVDFSV");

    if (platform_iop_module(&(Ps2IopModule){ .name = "cdvd_ee_driver", .path = "rom0:CDVDFSV" }, NULL, 0) != 0)
    {
        snprintf(runtime->error, runtime->capacity, "CDVDFSV LOAD FAILED");
        return PS2_CD_DRIVE_ERROR_CDVDFSV_LOAD;
    }

    static const unsigned servers[] = { PS2_RPC_CDVD_INIT, PS2_RPC_CDVD_SCMD, PS2_RPC_CDVD_NCMD };

    for (unsigned server = 0; server < sizeof(servers) / sizeof(servers[0]); ++server)
    {
        char message[64];

        snprintf(message, sizeof(message), "BINDING CD RPC %08X", servers[server]);
        runtime->stage(runtime->context, message);

        int ready = 0;

        for (int i = 0; i < PS2_RPC_PROBE_ATTEMPTS && *runtime->running; ++i)
        {
            if (platform_iop_probe(servers[server]) > 0)
            {
                ready = 1;

                break;
            }

            usleep(PS2_RPC_PROBE_DELAY_US);
        }

        if (!ready)
        {
            snprintf(runtime->error, runtime->capacity, "CD RPC %08X UNAVAILABLE", servers[server]);
            return PS2_CD_DRIVE_ERROR_RPC_UNAVAILABLE;
        }
    }

    /* No-disc init enables the driver without waiting forever for media. */
    runtime->stage(runtime->context, "INITIALIZING CD DRIVE");

    if (sceCdInit(SCECdINoD) <= 0)
    {
        snprintf(runtime->error, runtime->capacity, "CD DRIVE INIT FAILED");
        return PS2_CD_DRIVE_ERROR_INITIALIZE;
    }

    runtime->stage(runtime->context, "CHECKING DISC");

    return 0;
}

Ps2CdMedia platform_cd_drive_media(void)
{
    u32 changed = 0;

    sceCdTrayReq(SCECdTrayCheck, &changed);

    int type = sceCdGetDiskType();

    return (Ps2CdMedia){ changed != 0, type, type == SCECdCDDA, type >= SCECdDETCT && type <= SCECdDETCTDVDD, type == SCECdNODISC };
}

int platform_cd_drive_toc(Ps2CdToc* toc)
{
    /* GetToc issues its own command. Passive DiskReady may remain false on an idle drive. */
    memset(raw_toc, 0, sizeof(raw_toc));

    int result = sceCdGetToc(raw_toc);

    *toc = (Ps2CdToc){ .data = raw_toc, .size = sizeof(raw_toc), .error = sceCdGetError() };

    return (result && toc->error == SCECdErNO) ? 0 : PS2_CD_DRIVE_ERROR_TOC_READ;
}

int platform_cd_drive_idle(const Ps2CdDrive* d)
{
    return !d->failed && d->pending < 0;
}

int platform_cd_drive_failed(const Ps2CdDrive* d)
{
    return d->failed;
}

/**
 * @brief Request cancellation once and start its deadline.
 * @param d Drive state with a pending read.
 * @param now Monotonic milliseconds.
 */
static void cancel_read(Ps2CdDrive* d, uint32_t now)
{
    if (!d->cancelling)
    {
        d->cancelling = 1;
        d->cancel_at  = now;

        sceCdBreak();
    }
}

void platform_cd_drive_close(Ps2CdDrive* d)
{
    if (d->failed || d->pending < 0)
    {
        return;
    }

    cancel_read(d, platform_millis());

    while (d->pending >= 0 && !d->failed)
    {
        platform_cd_drive_poll(d, platform_millis());

        if (d->pending >= 0 && !d->failed)
        {
            usleep(DRIVE_POLL_DELAY_US);
        }
    }
}

int platform_cd_drive_reset(Ps2CdDrive* d, int position)
{
    platform_cd_drive_close(d);

    if (d->failed)
    {
        return PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;
    }

    d->bank       = 0;
    d->counts[1]  = 0;
    d->counts[0]  = 0;
    d->cursor     = position;
    d->completed  = 0;
    d->read_index = 0;
    d->buffered   = 0;
    d->chunked    = 0;

    return 0;
}

int platform_cd_drive_poll(Ps2CdDrive* d, uint32_t now)
{
    if (d->failed)
    {
        return PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;
    }

    if (d->pending >= 0)
    {
        if (!sceCdSync(1))
        {
            int success = sceCdGetError() == SCECdErNO && !d->cancelling;

            if (!success)
            {
                /* Retry exactly the failed range; already buffered music stays valid. */
                d->cursor -= d->counts[d->pending];
            }
            else
            {
                const uint8_t* data = (const uint8_t*)UNCACHED_SEG(sectors[d->pending]);

                for (int i = 0; i < d->counts[d->pending]; ++i)
                {
                    unsigned write = (d->read_index + d->buffered) % PS2_CD_RESERVE_SECTORS;

                    memcpy(reserve[write], data + i * CD_SECTOR_BYTES, CD_SECTOR_BYTES);
                    ++d->buffered;
                }

                ++d->completed;

                d->chunked |= d->buffered == PS2_CD_RESERVE_SECTORS;
            }

            d->counts[d->pending] = 0;
            d->bank               = 1 - d->pending;
            d->pending            = -1;
            d->cancelling         = 0;

            return success ? 0 : PS2_CD_DRIVE_ERROR_READ;
        }

        if (d->cancelling)
        {
            if ((uint32_t)(now - d->cancel_at) >= CANCEL_TIMEOUT_MS)
            {
                /* The drive may still DMA into the bank: leave it reserved. */
                dma_quarantined = 1;
                d->failed       = 1;

                return PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;
            }
        }
        else if ((uint32_t)(now - d->read_at) > READ_TIMEOUT_MS)
        {
            cancel_read(d, now);
        }
    }

    return d->pending >= 0 ? 1 : 0;
}

const uint8_t* platform_cd_drive_sector(Ps2CdDrive* d)
{
    return platform_cd_drive_waiting(d) ? NULL : reserve[d->read_index];
}

const uint8_t* platform_cd_drive_peek(const Ps2CdDrive* d, unsigned offset)
{
    return !d->failed && offset < d->buffered ? reserve[(d->read_index + offset) % PS2_CD_RESERVE_SECTORS] : NULL;
}

unsigned platform_cd_drive_buffered(const Ps2CdDrive* d)
{
    return d->failed ? 0 : d->buffered;
}

void platform_cd_drive_consume(Ps2CdDrive* d)
{
    if (platform_cd_drive_waiting(d))
    {
        return;
    }

    d->read_index = (d->read_index + 1) % PS2_CD_RESERVE_SECTORS;

    --d->buffered;

    d->chunked = 1;
}

int platform_cd_drive_prefetch(Ps2CdDrive* d, int end)
{
    if (d->failed)
    {
        return PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;
    }

    int empty = d->bank;

    if (d->pending < 0 && d->buffered < PS2_CD_RESERVE_SECTORS && d->cursor < end)
    {
        int n = end - d->cursor;

        if (n > READ_SECTORS)
        {
            n = READ_SECTORS;
        }

        unsigned free_sectors = PS2_CD_RESERVE_SECTORS - d->buffered;

        if ((unsigned)n > free_sectors)
        {
            if (d->chunked)
            {
                return 0;
            }

            n = (int)free_sectors;
        }

        /* Fixed 2x keeps the drive quiet with headroom for read-ahead. */
        sceCdRMode mode = { DRIVE_READ_RETRIES, SCECdSpinX2, SCECdSecS2352, 0 };

        if (!sceCdReadCDDA(d->cursor, n, sectors[empty], &mode))
        {
            return PS2_CD_DRIVE_ERROR_READ_START;
        }

        d->counts[empty] = n;
        d->pending       = empty;
        d->cursor += n;
        d->read_at = platform_millis();
    }

    return 0;
}

int platform_cd_drive_waiting(const Ps2CdDrive* d)
{
    return d->failed || !d->buffered;
}
