# CD recognition service

<!-- BEGIN TOC -->

## Contents

- [Run the recognition service](#run-the-recognition-service)
  - [Configuration](#configuration)
  - [Install as a background service (Linux)](#install-as-a-background-service-linux)
- [Recognition reference](#recognition-reference)
  - [Results and matching](#results-and-matching)
  - [Busy responses](#busy-responses)
  - [HTTP connections and traffic](#http-connections-and-traffic)
  - [Cache and retries](#cache-and-retries)
  - [Memory cache and report limits](#memory-cache-and-report-limits)
  - [Whole-job deadlines](#whole-job-deadlines)
  - [Local monitoring](#local-monitoring)

<!-- END TOC -->

Run this service on the computer supplying track labels and cover art. Commands below run from the repository root.

The [HTTP protocol](http-protocol.md) specifies the client API; the [standalone client example](examples/recognize.mjs) demonstrates it. The [implementation notes](implementation.md) describe server internals.

## Run the recognition service

Use Make, Node.js 24 LTS (latest 24.x patch) and FFmpeg on PATH. No npm packages
are required. With nvm, run these commands from the repository root to install
and select the version in `.nvmrc`:

```sh
nvm install
nvm use
```

Start the service:

```sh
make cd-recognize
```

On the first run, this creates `~/.config/stroom/config.json` and stops with a
message asking you to set `contact`. Open that file, enter your real contact
email or project URL, then run the command again. Both foreground runs and the
installed service read this same file. An absolute `XDG_CONFIG_HOME` changes its
location to `$XDG_CONFIG_HOME/stroom/config.json`.

The contact value identifies this client in provider requests. The service accepts requests from any client
address, including DHCP addresses. Allow inbound
**TCP 12890** on the service computer. HTTP carries recognition requests,
whole-disc results and cover downloads.

GnuDB requires a real contact email; a project URL skips this provider. Its
results must pass the track count and timing checks. A refusal disables further
GnuDB requests until the service restarts; other providers still run.

**Configure the PS2:** set the service's numeric IPv4 address in
[STROOM.DAT](../../../README.md#startup-configuration), then rebuild/copy the settings
and relaunch the player. Use its LAN address for local hosting or its public
address for internet hosting:

```ini
lookup_host=YOUR_SERVICE_IPV4
```

Recognition works in release and diagnostic builds. Clear `lookup_host` and
restart to disable it.

Use `node tools/cd/recognition/service.mjs --help` for command-line options.

### Configuration

[config.json](config.json) contains the complete defaults in one place. Your
file may include only `contact` and the settings you want to change; omitted
settings use those defaults. Sizes and speeds use readable units such as
`"1 GB"`, `"900 MB"` and `"10 MiB/s"`. GB/MB are
decimal units; GiB/MiB/KiB are binary units. Unit names are case-sensitive.

| Setting           | Meaning                                                                  |
| ----------------- | ------------------------------------------------------------------------ |
| `contact`         | Your provider contact email or project URL                               |
| `cache_directory` | Cache location; `null` selects the normal cache directory                |
| `cache_max_size`  | Disk cache size limit; defaults to `"1 GB"`                              |
| `cover_speed`     | Combined cover upload speed; defaults to `"10 MiB/s"`                    |
| `log_statistics`  | Enable [detailed log statistics](#local-monitoring); defaults to `false` |

A custom `cache_directory` may be absolute, start with `~/`, or be relative to
the configuration file. See [cache and retries](#cache-and-retries) for the
default location and eviction thresholds.
Unknown settings, unsupported units and invalid values stop startup with the
file path and an explanation.

Other protection limits are fixed in [config.mjs](config.mjs); see the
[recognition reference](#recognition-reference) for queues, traffic, cache
quotas and worker budgets.

Restart after editing the file. For the installed service:

```sh
systemctl --user restart stroom-cd-recognition.service
```

Settings changes need no reinstall. Use another file for a local run or installation
with `make cd-recognize CONFIG=/absolute/path/config.json` or
`make install-cd-recognition CONFIG=/absolute/path/config.json`.
Direct Node commands accept `--config FILE` instead. The installed unit remembers
that absolute path and reads it on each start. The installer never overwrites or
deletes your configuration.

### Install as a background service (Linux)

With systemd available, install under your current account without sudo:

```sh
make install-cd-recognition
```

Stop any foreground `make cd-recognize` process first, since both use TCP 12890.
The installer starts the background service and enables it at login. It captures
the active Node executable and FFmpeg location, including an nvm installation;
the service does not need an interactive shell. It copies the service and its
contract definitions into `~/.local/share/stroom/cd-recognition`, so it runs
independently of the checkout. The unit is installed at
`~/.config/systemd/user/stroom-cd-recognition.service`. `XDG_DATA_HOME` and
`XDG_CONFIG_HOME` override those base directories when set to absolute paths.

Reinstall with the same command after updating the code or selecting a different Node installation.
Installation validates the copied service before stopping an existing managed
instance and restores the previous files if replacement startup fails.

Control the installed service and follow its logs with:

```sh
systemctl --user status stroom-cd-recognition.service
systemctl --user stop stroom-cd-recognition.service
systemctl --user start stroom-cd-recognition.service
journalctl --user -u stroom-cd-recognition.service -f
```

The user service restarts after crashes, waiting five seconds between attempts,
with at most five starts in five minutes to bound repeated failures. After fixing
such a failure, run `systemctl --user reset-failed stroom-cd-recognition.service`
before starting it again. Shutdown allows up to 150 seconds for active work and
then kills remaining children. The configured resource limits cover Node and
FFmpeg together:

| Resource        | Initial limit                                        |
| --------------- | ---------------------------------------------------- |
| Memory pressure | Throttle above 512 MiB                               |
| Memory maximum  | 1 GiB, without swap; an OOM kill terminates the unit |
| CPU time        | One CPU core's worth across all processes            |
| Tasks           | 128 combined processes and threads                   |

These are initial limits to validate against your workload. CPU, memory and task
enforcement requires cgroup v2 and controller delegation to your systemd user
manager. The installer checks the resulting kernel limits and warns if they
cannot be confirmed. Inspect usage and the service's control group with:

```sh
systemctl --user show stroom-cd-recognition.service -p ControlGroup -p MemoryCurrent -p MemoryPeak -p TasksCurrent
```

The installer does not modify system-wide controller delegation, journal
retention, firewall rules or account settings. For a machine serving users while
you are logged out, optionally enable user lingering:

```sh
loginctl enable-linger
```

This starts your user manager at boot and keeps it running after logout; see
[systemd's lingering documentation](https://www.freedesktop.org/software/systemd/man/latest/loginctl.html#enable-linger%20%5BUSER%E2%80%A6%5D).
Without lingering, automatic startup follows your login session.

To stop and uninstall the background service while retaining configuration, cache and journal
records:

```sh
make uninstall-cd-recognition
```

The foreground `make cd-recognize` command remains available on Linux and macOS.

## Recognition reference

### Results and matching

MusicBrainz is tried first, then GnuDB, then CTDB candidate names checked against
Discogs track lists and timings. Several consoles can share the service, including
while playing the same disc. Recognition and cover downloads are shared per disc;
each console stores all track labels locally, and changing discs on one console
does not affect the others. Track changes need no recognition request.

The service publishes recognized labels before searching for artwork. Covers are
converted to smaller JPEGs and served locally, so the PS2 does not contact the
catalog providers directly. Missing or failed artwork does not reject recognized
metadata. A slow cover request does not block recognition of a new disc.

The terminal prints one result line with the selected artist and album, disc and
track count, artwork status, lookup time and report path. Unmatched lookups show
the candidate count instead. Saved JSON reports retain the disc layout, candidates,
selected release, track lists and matching evidence. A lookup error is distinct
from finding no acceptable match.

Matching uses track count and timings, not album duration alone. Exact disc IDs
can identify several releases with the same layout; approximate matches require
additional checks. Reports expose the applied criteria and discrepancies.
A successful match does not prove a particular physical edition. Multi-disc
Discogs releases are compared one disc at a time, so a matching bonus disc can
supply the release artwork even when another disc has extra tracks.

Cover resolution tries the selected release, then matching MusicBrainz releases
already retrieved, before searching Discogs. Artwork from another edition must
be corroborated by artist, album and track information. Borrowing a cover never
replaces the selected track metadata. The report records the cover's source.
For numbered series, artwork searches can try the main album title with Arabic
and Roman volume numbers, using at most four search variants and eight fetched
Discogs releases. Volume numbers remain significant. Differences in subtitles,
trailing `Pres.`/`Presents` credits and `Jr.`/`Junior` spellings are accepted only
when every track name agrees in order after these credit aliases, regardless of
disc length (for example, all six names on a six-track release must match).
This can establish artwork when Discogs omits timings; it does not
change recognized labels or metadata matching. Saved reports record this basis
as `artist-album-track-name-aliases`.
When GnuDB omits a radio-edit suffix or spells a possessive differently, artwork
matching can accept those variants for at least 90% of ten or more ordered
tracks, provided every duration is present and the normal timing limits pass.
Other mix qualifiers remain significant. These artwork variants do not relax
metadata recognition or enable its isolated timing-error exception. Detailed
artwork rejections and download errors are retained in the report; the result
line shows whether artwork is available, pending or unavailable.

The PS2 currently accepts audio-only TOCs and rejects entries marked as data;
it does not normalize CD-Extra sessions. Other clients can submit normalized
CD-Extra audio TOCs using the [conversion rules](http-protocol.md#appendix-b-disc-types-and-cd-extra-normalization).
Data-first and other unsupported mixed layouts must not be submitted.

### Busy responses

HTTP 429 and 503 responses include a JSON error and a Retry-After delay.
All advertised retry delays are capped at **seven days**; longer provider
deadlines remain enforced on the server.
See the [PS2 client profile](http-protocol.md#10-ps2-client-profile) for probe,
polling and track-change retries. A busy POST does not acknowledge a job, even
if its error includes a disc ID.

TOC bodies are limited to **1,024 bytes**, checked before JSON parsing. Invalid requests receive
bounded errors without logging submitted payloads or starting provider work.
Duplicate JSON keys are rejected, including equivalent escaped names.

New uncached recognition work is limited separately per public source IP:
at most **one waiting disc lookup plus a running lookup**, with an admission allowance of
two lookups replenished at **six per minute** (one every ten seconds). Devices
behind the same router share this allowance. The service checks disk results
before charging it; cached labels, unexpired no-match/error results and requests
joining an existing lookup do not consume another allowance. Expired outcomes
require a new admission when recognition retries.

Recognition permits **three waiting jobs plus one running job** globally. A full
waiting queue returns `503 busy` with a 15-second delay for new uncached work;
cached results and joining accepted jobs remain available.

Queued recognition takes turns between callers. A running lookup finishes before
the next caller's turn; artwork remains on its separate queue. A rejected lookup
receives a busy reply without contacting providers or saving a failure report.
Three or more callers can saturate the queue and compete for newly free slots,
leaving other uncached lookups rejected. Round-robin order protects admitted jobs,
not future admission; this is a known limit of the unauthenticated service’s
single recognition worker.

### HTTP connections and traffic

The HTTP service applies these fixed limits to direct TCP peer addresses:

| Resource                                                          | Global limit               | Per-IP limit               |
| ----------------------------------------------------------------- | -------------------------- | -------------------------- |
| Open TCP connections, including silent or partial requests        | 64                         | 8                          |
| Unfinished cover transfers, including outstanding file operations | 8                          | 1 (fixed)                  |
| All HTTP requests                                                 | 20 per second, burst of 40 | 60 per minute, burst of 10 |
| Cover GET requests                                                | Shared HTTP allowance      | 10 per minute, burst of 5  |

Cover-transfer slots include file reads and image bytes waiting to be sent.
A slot is released when the response finishes sending or the connection closes,
after any outstanding file operation settles. Full cover slots return HTTP `503`
with `Retry-After: 15` and a closing connection, without reading another image.
Each admitted download has a **30-second total deadline**, including file access
and sending; the service closes connections that exceed it and cancels interrupted
reads. Metadata requests remain independent of these slots.

Request allowances replenish continuously: one request every 50 milliseconds
globally and one per second per IP. Covers also spend a separate allowance
replenished at one every six seconds per IP. Reconnecting does not reset an IP's
allowance. All requests count before routing or file access, including invalid
methods and missing paths. Per-IP checks run first, so requests already over that
caller's limit do not spend the shared allowance. A per-IP request rejection returns HTTP `429`; a global
request rejection returns `503`, both with a `Retry-After` delay rounded up to
whole seconds.
Excess TCP connections are closed before request processing.

Each connection serves one response and closes. Pipelined requests, HTTP upgrades,
CONNECT tunnels and malformed requests close the connection without provider or
image access. Only the TOC POST accepts a body. Complete request headers and
the bounded POST body must arrive within five seconds of TCP acceptance; sending
occasional bytes does not extend this deadline. Response connections also have a 30-second inactivity
timeout, alongside each admitted cover's 30-second total deadline.

Per-IP state is capped at 1,024 entries. Idle entries whose request allowances
have fully replenished can be removed; active connections, cover work and spent
allowances are protected. If no entry can be removed, connections from untracked
IPs are rejected until space becomes available. Callers sharing a public IP
share its limits. All allowances use the actual TCP peer IPv4 address;
forwarding headers never change caller identity. The service is hosted directly
over HTTP on TCP 12890, without a reverse proxy.

Cover response bodies additionally share the `cover_speed` limit, **10 MiB/s**
by default, with a **1 MiB** immediate burst. Once the burst is spent, downloads
wait for their share of that speed; they keep their active-transfer slot and
deadline. Pacing respects socket backpressure, and disconnects cancel
waiting work. Metadata responses remain independent. This bounds cover data
submitted to sockets; network headers and retransmissions consume additional
traffic. It is not a monthly hosting traffic allowance.

### Cache and retries

Recognized results and processed covers persist across restarts. The cache uses
`$XDG_CACHE_HOME/stroom/cd` when XDG_CACHE_HOME is absolute; otherwise it uses
`~/.cache/stroom/cd` on Linux or `~/Library/Caches/stroom/cd` on macOS.
`cache_directory` in `config.json` selects another location.

The service limits disc reports and processed covers by bytes, cached-disc count
and file count. Default thresholds are:

| Resource     | Start eviction above           | Prune down to                  |
| ------------ | ------------------------------ | ------------------------------ |
| Disk bytes   | **1 GB (1,000,000,000 bytes)** | **900 MB (900,000,000 bytes)** |
| Cached discs | **10,000**                     | **9,000**                      |
| Cache files  | **20,000**                     | **18,000**                     |

Crossing any upper limit starts eviction of the least recently used discs,
including their reports and covers, until all three lower targets are met.
The byte thresholds follow `cache_max_size`; cleanup targets 90% of that value.
The count limits bound the disk index and the number of small reports and files,
independently of the retained-result RAM allowance. A disc with only orphaned
covers or a temporary report still counts as one cached disc. Every owned file
counts, including zero-byte files and multiple covers for one disc.
It checks existing files at startup and accounts for new writes while running.
Access times survive restarts and are recorded at most once per minute per disc.
Orphaned covers and leftover report temporary files count toward the quota;
provider cooldown files and unrelated files are preserved and excluded.
Active jobs and file reads protect their discs from eviction. If these prevent
cleanup, usage can temporarily exceed the limits; cleanup retries when reads or
jobs finish.
Evicted discs are looked up again when requested. Use one service per cache
directory so its accounting and file maintenance remain coordinated.

Completed lookups with no acceptable match are cached for **24 hours**. Lookup
errors are cached separately for **60 seconds**, or until a longer provider
`Retry-After` deadline. A provider failure never becomes a no-match result.
No-match HTTP responses expose their remaining cache lifetime as
`expires_in_seconds`; this is not a retry instruction. These fixed deadlines
survive restarts and in-memory eviction; repeated requests
do not extend them. After expiry, the next TOC POST can repeat recognition. A result GET never starts
recognition and returns `404` for an expired outcome. Invalid
TOCs are rejected without creating a cache entry. Unsuccessful reports share the
disk quota with recognized discs. If reports cannot be saved, the same deadlines
still apply in memory until eviction or restart.

Use `--refresh` to bypass saved recognition, no-match results and lookup errors
and repeat provider lookups, for example after changing matching rules. Invalid
cache entries are ignored. Missing
cached images retain the labels and can trigger artwork recovery. Temporary
artwork failures retry separately from recognition. Their retry deadlines are
saved in the existing per-disc report, so restarting does not reset the cooldown.
Retries wait at least a minute after a failure and respect longer provider
`Retry-After` delays. Provider deadlines are also saved separately in the cache
and restored for both recognition and artwork, even for a different disc or with
`--refresh`. If storage is unavailable, cooldowns remain in memory only.

Unfinished artwork without a deadline, or with an expired one, queues as soon as
the lookup result is requested; normal provider pacing still applies. Confirmed
absence of a cover can be cached. Report-write failures are logged without
discarding an otherwise successful lookup in memory. If the cache directory cannot be created at startup,
the service continues with labels and in-memory recognition only. Disk caching
and artwork stay disabled until the service is restarted with usable storage.

Client artwork polling and failure recovery follow the
[protocol's artwork rules](http-protocol.md#7-artwork).

### Memory cache and report limits

The service retains up to **64 MiB** of compact results within **128 shared disc
records**. Queued and running jobs are protected; idle results leave RAM when
space is needed, and saved reports can be loaded again. Active jobs, image
buffers and Node object overhead require additional memory.

See [memory accounting](implementation.md#memory-and-disk-cache) and
[report bounds](implementation.md#report-bounds) for retention, publication and
provider-data limits.

### Whole-job deadlines

Recognition has a **60-second** work budget and artwork has a separate
**120-second** budget, starting when each worker takes its job. Queue waiting
and final outcome saving are separate. A recognition timeout is a temporary
failure; an artwork timeout retains labels and permits recovery after its
cooldown. See [worker budgets](implementation.md#worker-budgets) for cancellation,
persistence and retry details.

### Local monitoring

Normal runs print startup messages, short queue and cache-hit messages, one-line
results, warnings, critical events and recovery events. For detailed JSON
summaries, set `log_statistics` to `true` and
[restart the service](#configuration). Warnings and errors are always logged;
this setting does not change systemd's restart policy.

With statistics logging enabled, the service writes a `CD monitor: ` JSON summary at startup and
every minute. It reports recognition and artwork queue depths separately, running jobs,
the age of the oldest waiting job, and matched/no-match/failed lookup counts since
the previous summary. Idle cached discs, cache reads, and deferred artwork retry
timers do not count as queued provider work. Cache hits do not repeat lookup
outcome counts.

The `memoryCache` summary reports retained bytes and their allowance, retained
report count, and cumulative RAM evictions. Disk evictions remain a separate
counter under `cache`.

The `http` summary includes open connections, unfinished cover transfers, tracked
IP count and configured limits. Its `rejected` counters are cumulative;
`rejectedSinceLastSummary` reports new connection, request, cover, header-timeout,
malformed, unsupported and pipelined rejections by reason. These aggregate records
do not include caller addresses or request contents, and rejected traffic does
not produce individual log lines.

Provider counters cover the last five minutes, using one-second buckets, grouped
by request hostname. They include completed HTTP requests, transport/HTTP/body
failures, decoding or validation errors (`protocolErrors`), timeouts, HTTP 429s,
expected HTTP 404s, requests skipped during cooldown, in-flight requests, the last
successful HTTP response time, and active cooldown deadlines. Requests cancelled
by a whole-job deadline have a separate
`cancelled` counter and do not count as provider failures or successful responses;
the existing `timeouts` counter counts individual request timeouts. No-match results
and expected 404s are not provider failures. Transport failures caught again by a
provider parser are counted once. Host labels are bounded to 64 entries, including
an `other` group for additional hosts.

Storage summaries include accounted cache bytes, disc/file counts and all three
sets of limits and targets, completed disc evictions since the previous summary,
available bytes on the cache filesystem,
storage failures since the previous summary, and operations with unresolved
errors. Available space is sampled using the service user's available filesystem
blocks. Cache size uses existing quota accounting without rescanning the cache.
An unavailable filesystem sample is reported as `null`, not zero free space.

Warning checks run at startup and every minute, with or without detailed summaries:

| Condition            | Warning threshold                                                                                                    |
| -------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Recognition backlog  | Oldest waiting job over 60 seconds on two consecutive samples                                                        |
| Artwork backlog      | Oldest waiting job over 3 minutes                                                                                    |
| Provider errors      | At least 5 failures (including protocol errors) and at least 50% failed completed requests in the five-minute window |
| Cache quota          | Bytes above the configured cache limit, discs above 10,000, or files above 20,000 for 5 minutes                      |
| Available disk space | Below 1 GB; critical below 250 MB                                                                                    |
| Storage operation    | Immediately after a read, write, scan, timestamp update, deletion, or filesystem-sample failure                      |
| Disabled cache       | Cache directory unavailable at startup                                                                               |

Unresolved warnings repeat at most every five minutes; severity changes and
recovery are logged immediately when observed. Storage errors clear after the
same operation succeeds again; an unrelated successful operation does not clear
them. These thresholds provide initial operational guidance and do not trigger
process restarts or change request handling. Monitor records contain aggregate
counts, hostnames, and error codes, without caller addresses or request payloads.

Records go to the foreground terminal or the
[installed service's journal](#install-as-a-background-service-linux).
Monitoring performs no provider requests and adds no public endpoint or external
notification service.
