# CD recognition service implementation notes

These notes describe this repository's service defaults and resource accounting.
Client requests, responses and retry policies are specified in the
[HTTP protocol](http-protocol.md). The [CD tools reference](README.md#run-the-recognition-service)
covers running the service, cache behavior and local monitoring.

The [configuration defaults](config.json) collect operator settings. See the
[configuration reference](README.md#configuration) for units, locations and restart
behavior. Cache size and cover upload speed are configurable; other resource
limits and protection rules are fixed in [config.mjs](config.mjs). Numeric cache
sizes in these notes describe the shipped defaults.

## Memory and disk cache

| Resource                      | Limit                                                     |
| ----------------------------- | --------------------------------------------------------- |
| Shared in-memory disc records | 128 globally, across all work states and retained results |
| Retained compact result data  | 64 MiB within those same 128 records                      |

The **128-record limit** counts distinct disc IDs registered in memory, not
clients, TCP requests or 128 jobs per queue. One record is shared by cache loading,
queued/running recognition, queued/running artwork and any retained idle result
for that disc. Recognition and artwork for one disc consume one record together.
Each running provider worker is included, not an extra slot beyond the 128.
This memory bound is separate from the three-slot recognition waiting queue.
On-disk-only results, readiness probes, HTTP connections and image transfers do
not independently consume disc records. If all records are occupied by protected
work, a new uncached POST receives `503 busy` without accepting the lookup.

The retained-data allowance counts serialized UTF-8 report bytes. Node objects,
active provider jobs and image buffers require additional RAM; it does not bound
total process memory. The byte allowance and record limit apply simultaneously.

Repeated requests update the result's position in an access-order LRU. When the
byte allowance or record limit requires room, the oldest idle results leave
RAM. Their saved reports and covers remain on disk and can be loaded again
without contacting providers. Running and queued jobs are protected from RAM
eviction. If no idle result can make enough room, the service attempts to save
the new result, then releases that job. An accepted lookup may remain pending
until that save finishes. Cached disk results can be delivered without retaining
a RAM entry. Artwork recovery waits for a later status request with job capacity.

Compact reports retain candidates, labels, cover choices and rejection state,
matching evidence and retry deadlines. Raw MusicBrainz/CTDB/Discogs responses,
duplicate provider candidate lists and diagnostic track calculations stay in the
full saved report. The active artwork worker reloads that full report for updates,
preserving disk diagnostics. When saving or reloading is unavailable, its compact
copy still supports artwork recovery, possibly requiring provider requests again;
provider cooldowns continue to apply.

Disk-cache byte, disc and file quotas, eviction protection and expiry rules are
documented in the [cache reference](README.md#cache-and-retries).

## Report bounds

Individual reports and provider data have these bounds:

| Data                                                 | Limit                        |
| ---------------------------------------------------- | ---------------------------- |
| Retained compact report                              | 2 MiB                        |
| Full saved JSON report                               | 16 MiB, including formatting |
| Recognition candidates / artwork fallback candidates | 64 in each list              |
| Media per candidate / MusicBrainz release            | 32                           |
| Tracks per medium                                    | 99                           |
| MusicBrainz releases before mapping or ranking       | 64                           |
| CTDB candidates                                      | First 5 valid candidates     |
| Discogs search seeds / retained search records       | 5 / 32                       |

Provider responses are limited to 4 MiB, or 5 MiB for source cover images.
Artwork searches fetch at most eight Discogs releases. Excessive candidate sets
fail as temporary lookup errors
instead of selecting a match from truncated evidence or caching a no-match result.
Oversized saved files are rejected before reading or parsing their contents;
reads remain bounded even if a file grows during access. Unsupported or
oversized disk reports are treated as cache misses. A rejected report write does
not replace the last good saved copy.

## Worker budgets

| Resource                                        | Limit                                         |
| ----------------------------------------------- | --------------------------------------------- |
| Active provider workers                         | One recognition worker and one artwork worker |
| Recognition work budget                         | 60 seconds after its worker starts            |
| Artwork work budget                             | 120 seconds after its worker starts           |
| Individual provider request / FFmpeg conversion | 15 seconds each                               |

Each budget starts when its worker takes the job; time waiting in the queue does
not count. Provider pacing, network requests, response processing and artwork
conversion consume that same budget.
Artwork also includes reloading its saved report and writing the prepared image.

Expiry cancels provider waits and requests and kills an active FFmpeg converter.
The worker waits for cancellation cleanup before accepting another job. Checks
use a monotonic clock, including after parsing, so wall-clock changes cannot
extend a budget and a late result cannot be published as a successful attempt.
Synchronous processing and filesystem operations finish at their next cancellation
check; these are work budgets rather than hard process execution limits.

A recognition timeout is saved as a temporary failure, with retries allowed on
a later TOC submission after at least 60 seconds. It is not a 24-hour no-match result.
An artwork timeout retains the recognized labels and allows artwork to be retried
after at least 60 seconds. Both retain any longer provider `Retry-After` deadline;
cancellation does not permanently reject an artwork URL. Final outcome saving
and cache accounting follow the attempt and are allowed to finish before the
worker is released. Shared provider-cooldown writes may finish independently.

Serving an already cached cover continues to use its separate 30-second download
deadline. Cache hits do not start a recognition attempt.

The [systemd user installation](README.md#install-as-a-background-service-linux)
limits host CPU, memory and tasks for Node and FFmpeg together, subject to
controller delegation. FFmpeg uses at most two decoder and encoder threads each
and one filter thread.

With at most three waiting recognition jobs and one running job, 60 seconds of
active work per job means the last admitted lookup can require about four minutes
of worker time including its own recognition. Final persistence, cancellation
cleanup and network delays can still exceed the client's five-minute waiting
window; acceptance is not a completion SLA.

## Artwork retry persistence

The service persists artwork retry state and its absolute retry deadline in the
same report as the matched labels. RAM eviction and restart reload the remaining
delay without resetting or shortening it. This guarantee requires a successfully
saved report that has not been removed or disk-evicted. Failed writes retain the
new deadline only in RAM; storage failures are logged. No restart/eviction
durability is promised for unsaved state.
