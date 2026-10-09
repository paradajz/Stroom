# CD recognition

<!-- BEGIN TOC -->

## Contents

- [Run the service](#run-the-service)
- [Results and matching](#results-and-matching)
- [Busy responses](#busy-responses)
- [Cache and retries](#cache-and-retries)

<!-- END TOC -->

This optional PC service identifies a CD from its track layout and sends labels
and cover art to the PS2. CD playback works without the service.

## Run the service

Use Node.js 20 or newer and FFmpeg on PATH. No npm packages are required.
From the repository root on the PC or Mac:

```sh
node tools/cd/service.mjs --contact "YOUR_REAL_CONTACT_EMAIL"
```

The contact value identifies this client in provider requests. Replace it with
your contact email or project URL. By default the service accepts disc requests
from any console address, including DHCP addresses. Use `--ps2 ADDRESS` to restrict
requests to one address. Allow inbound
**UDP and TCP 12890** on the service computer: UDP carries lookup messages and TCP
serves covers.

GnuDB requires a real contact email; a project URL skips this provider. Its
results must pass the track count and timing checks. A refusal disables further
GnuDB requests until the service restarts; other providers still run.

Set the service computer's LAN IPv4 address as `lookup_host` using the
[startup configuration guide](../../README.md#startup-configuration), then
restart the player. Recognition works in release and diagnostic builds.
To disable it, clear `lookup_host` and restart.

Use `node tools/cd/service.mjs --help` for command-line options.

## Results and matching

MusicBrainz is tried first, then GnuDB, then CTDB candidate names checked against
Discogs track lists and timings. Several consoles can share the service, including
while playing the same disc. Recognition and cover downloads are shared per disc;
each console receives updates for its own track, and changing discs on one console
does not affect the others.

The service publishes recognized labels before searching for artwork. Covers are
converted to smaller JPEGs and served locally, so the PS2 does not contact the
catalog providers directly. Missing or failed artwork does not reject recognized
metadata. A slow cover request does not block recognition of a new disc.

The terminal prints candidates and identifies **BEST MATCH** when a release is
selected. Saved JSON reports retain the disc layout, candidates, selected release
and matching evidence. A lookup error is distinct from finding no acceptable
match.

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
Recognition supports the audio-only discs handled by the player; it is not a
mixed-mode disc cataloging tool.

## Busy responses

When the service is busy, it asks the console to retry after a delay. Cached
labels and covers can still be returned; labels with unfinished artwork may be
followed by a busy reply. Updated players honor the delay, keep existing metadata
while waiting, and clear the wait on a track or disc change. Older players ignore
busy replies and continue their normal retries; ordinary metadata replies remain
compatible. UDP packets can be lost during a flood, so clients retry.

## Cache and retries

Recognized results and processed covers persist across restarts. The cache uses
`$XDG_CACHE_HOME/stroom/cd` when XDG_CACHE_HOME is absolute; otherwise it uses
`~/.cache/stroom/cd` on Linux or `~/Library/Caches/stroom/cd` on macOS.
`--out DIRECTORY` selects another location.

Use `--refresh` to bypass saved recognition and repeat provider lookups, for
example after changing matching rules. Invalid cache entries are ignored. Missing
cached images retain the labels and can trigger artwork recovery. Temporary
artwork failures retry separately from recognition. Their retry deadlines are
saved in the existing per-disc report, so restarting does not reset the cooldown.
Retries wait at least a minute after a failure and respect longer provider
`Retry-After` delays. Provider deadlines are also saved separately in the cache
and restored for both recognition and artwork, even for a different disc or with
`--refresh`. If storage is unavailable, cooldowns remain in memory only.

Unfinished artwork without a deadline, or with an expired one, queues as soon as
the cached disc is requested; normal provider pacing still applies. Confirmed
absence of a cover can be cached. Report-write failures are logged without
discarding an otherwise successful lookup in memory. If the cache directory cannot be created at startup,
the service continues with labels and in-memory recognition only. Disk caching
and artwork stay disabled until the service is restarted with usable storage.

The PS2 retries while recognition or artwork is incomplete, so the service can
be started after the CD. Once the current track has metadata and its cover has
downloaded—or the service confirms that no cover will be supplied—lookup requests
and reply polling stop. Temporary artwork failures remain pending for retries.
A track change requests its labels; an unchanged cover URL reuses the downloaded
artwork.
Responses are tied to the requested disc and track to reject stale replies.
