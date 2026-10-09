# CD tools

<!-- BEGIN TOC -->

## Contents

- [Stream CD audio to this computer](#stream-cd-audio-to-this-computer)
  - [Continuous playback and reconnection](#continuous-playback-and-reconnection)
  - [PC buffering](#pc-buffering)
- [Discovery and firewall](#discovery-and-firewall)
  - [firewalld exception](#firewalld-exception)
- [Diagnose CD dropouts](#diagnose-cd-dropouts)
  - [Timing and counters](#timing-and-counters)
  - [Capture an existing listener](#capture-an-existing-listener)
  - [Start a listener and capture together](#start-a-listener-and-capture-together)
  - [Read a saved capture](#read-a-saved-capture)
  - [SDK and launcher updates](#sdk-and-launcher-updates)
- [Run the recognition service](#run-the-recognition-service)
- [Results and matching](#results-and-matching)
- [Busy responses](#busy-responses)
- [Cache and retries](#cache-and-retries)

<!-- END TOC -->

These tools run on the computer receiving CD audio or supplying track labels and
cover art. Run commands from the repository root in that computer's terminal.

| File                         | Purpose                                                   | How it runs                                                             |
| ---------------------------- | --------------------------------------------------------- | ----------------------------------------------------------------------- |
| [listener.mjs](listener.mjs) | Receives PS2 CD audio and plays it through FFplay.        | Start it with the command below.                                        |
| [playback.mjs](playback.mjs) | Manages the listener's startup and recovery buffer.       | Imported automatically by the listener.                                 |
| [service.mjs](service.mjs)   | Identifies discs and supplies track labels and cover art. | Start the optional [recognition service](#run-the-recognition-service). |

The listener and recognition service work independently and can run together.
Local PS2 CD playback works with neither PC tool running.

## Stream CD audio to this computer

With Node.js 18 or newer and FFplay installed, run:

```sh
node tools/cd/listener.mjs
```

Keep CD selected on the PS2. You can start the listener before or after CD
playback; it discovers the console and waits for audio. No PC destination
address or output selection is configured on the PS2. Local PS2 sound stays
enabled.

To list discovered consoles or select one directly:

```sh
node tools/cd/listener.mjs --list
node tools/cd/listener.mjs --host YOUR_PS2_IP
```

Use `--host` if discovery fails or several consoles are found. The listener
queries each local IPv4 subnet; `--broadcast ADDRESS` overrides its discovery
destinations. A bridged development container generally cannot discover devices
on the host LAN through broadcast, although a direct `--host` connection may work.

Discovery uses **UDP 12891**; audio uses **TCP 12891**, with up to four listeners.
Permit these connections between the devices. This outgoing CD transport is
separate from incoming AriaCast audio.

FFplay is distributed with many FFmpeg installations. If its executable is
elsewhere, pass `--ffplay /path/to/ffplay`. A missing or inaccessible executable
is reported with its path and exits with a failure status. Use
`node tools/cd/listener.mjs --help` for all options.

Audio is **48 kHz signed 16-bit little-endian stereo PCM**, copied from successful
CD resampler output. Track controls stay on the PS2. Sound/L2 affect local PS2
volume; control PC volume on the computer. The stream carries audio only.
PS2 and PC playback have independent buffering and are not synchronized.

### Continuous playback and reconnection

Leave the listener running for continuous use. It reconnects after manual track
changes, pause, seeking, disc changes or a lost connection. Each new session
gets a fresh player and audio buffer. Automatic Repeat, Shuffle and Program
transitions keep the same connection and player, preserving queued audio as the
next track starts.

Manual track changes, seeks, pauses and stops cancel old buffered audio. When
playback has no following track, an explicit end lets PC playback drain, including a clean clip
shorter than the configured buffer. Connections made during the opening of an
active session receive its beginning while it still fits in the PS2's transport
queue. This prevents discovery or reconnection from clipping the first notes.
Later connections start at the current submitted audio; completed sessions are
not replayed.

TCP keepalive detects connections lost without a clean disconnect. If a
discovered console becomes unavailable, the listener discovers again, including
after its address changes. A healthy connection can wait while playback is
paused or stopped, without an audio-inactivity deadline. FFplay shutdown is
bounded so a stuck player cannot block subsequent sessions. Use `--once` to exit
after one session instead of reconnecting.

Slow connections are disconnected and retried without stalling local CD playback.
After an interruption, the listener queries the console for its last disconnect
reason and sender counters. See [server queue and timeout policy](../../src/common/audio/README.md#outgoing-cd-audio)
for the opening window and transport limits.

### PC buffering

The listener collects received PCM before starting playback, giving the PC a
reserve for short network delivery gaps. For example:

```sh
node tools/cd/listener.mjs --buffer-ms 1000
```

This selects a one-second reserve. `--buffer-ms 0` disables PC startup and
recovery buffering. The default and limits are defined in
[PC_BUFFER](playback.mjs) and printed by `--help`. A larger reserve adds startup
and recovery delay.

If delivery exhausts the estimated reserve, the listener logs `Rebuffering ...`,
collects the configured amount again, then logs `resuming playback` and resumes
feeding FFplay. It preserves sample order and keeps the same connection and
player. The estimate uses submitted PCM duration and elapsed time; FFplay and
the sound device can have additional latency.

The PC reserve covers network delivery gaps. The PS2's separate disc reserve
covers interrupted CD reads; see [disc and PC buffers](../../troubleshooting.md#disc-and-pc-buffers).

## Discovery and firewall

If direct streaming with `--host` works but automatic discovery fails on Linux,
leave the listener running and use a second terminal on the same PC:

```sh
node tools/cd/diagnose-discovery.mjs YOUR_PS2_IP
```

The script finds the LAN interface, requests sudo for a seven-second UDP packet
capture and read-only firewall inspection, and compares broadcast with direct
discovery. It requires `ip`, `tcpdump` and `timeout`; it changes no network or
firewall settings. Use `--firewall-only` to inspect the firewall without repeating
the capture. Share the complete output to locate where discovery fails.

The listener uses a temporary UDP reply port chosen by the operating system.
Broadcast queries receive unicast replies from the PS2's UDP source port 12891,
which a PC firewall may not associate with the broadcast query. If the capture
sees replies but the listener does not, check rules for those incoming replies.
Opening UDP destination port 12891 on the PC does not cover the listener's reply
port.

### firewalld exception

Test a five-minute exception restricted to the PS2's address and UDP source port.
Replace the zone and address with those reported by the diagnostic script:

```sh
sudo firewall-cmd --zone=YOUR_LAN_ZONE --timeout=5m --add-rich-rule='rule family="ipv4" source address="YOUR_PS2_IP" source-port port="12891" protocol="udp" accept'
```

Repeat discovery with `node tools/cd/listener.mjs --list`. The rule expires
automatically and does not change permanent configuration or require a firewall
reload. See firewalld's [rich rule syntax](https://firewalld.org/documentation/man-pages/firewalld.richlanguage.html)
and [rule timeouts](https://firewalld.org/documentation/man-pages/firewall-cmd.html).

If the test restores discovery, save the same PS2-only exception with:

```sh
node tools/cd/allow-discovery.mjs YOUR_PS2_IP
```

This script finds the LAN zone, requests sudo to save the rule permanently, and
replaces any timed runtime copy with an untimed copy. It does not reload the
firewall. Then run `node tools/cd/listener.mjs` for automatic discovery and playback.

## Diagnose CD dropouts

Start with listener timing and source counters:

```sh
node tools/cd/listener.mjs --stats
```

The listener and its counter queries work in release builds. Compare source
timing with PC delivery before changing buffering; see the
[outgoing CD troubleshooting path](../../troubleshooting.md#outgoing-cd-audio).

### Timing and counters

Every ten seconds, `--stats` reports received audio duration, elapsed receive
time, the largest arrival gap and the largest wait while writing to FFplay.
Arrival gaps include local processing delays and do not directly measure the
sound device's buffer. The PC reserve can cover short delivery gaps; it does not
prevent TCP send stalls.

PS2 source counters report submitted sectors, gaps over 40 ms, the longest
submit gap, drive-wait polls, local output-busy polls, contiguous wait maxima and
underrun silence insertions, excluding lead-out silence. These reset on manual
track changes, seeks, pauses and stops; automatic track transitions retain them.
Sender retry counts and queue maxima persist for the stream server's lifetime.
Restart the app between wired and Wi-Fi comparison runs to give those counters
matching reset points.

Queue peaks count audio sectors waiting to be sent. Opening catch-up can raise
the peak even on a healthy wired connection. A peak alone does not establish a
network stall; check arrival gaps and send-stall reports alongside it. Start
comparison runs with the same listener connection timing as well as the same
track, buffer setting and observation duration.

With a current SDK and app, `--stats` also prints `PS2 CD TCP diagnostics
available`. After a continuous send retry lasts at least 250 ms, the PS2 saves
TCP state and refreshes it at most every 250 ms while the stall continues.
`PS2 CD TCP stall` reports its duration, sample time, queued sectors, TCP send
space, unsent and unacknowledged bytes, packet reference counts, sequence
numbers, windows and retransmission timer.

The latest snapshot survives recovery and playback resets, so it can describe
an earlier session. Check whether its stall ID changes. Repeated identical
reports are suppressed. These diagnostic queries open no audio connection.
An unavailable TCP option is reported explicitly, indicating an older resident
stack or a failed socket query.

Stall reports also include Ethernet transmit drops, errors, carrier losses,
excessive deferrals, collisions and FIFO underruns, plus receive allocation
failures, overruns and bad frame checksums. These console-boot counters are
sampled at the stall; compare successive snapshots for new errors. Link modes
1/2 mean 10 Mbps half/full duplex; 3/4 mean 100 Mbps half/full duplex.

The Ethernet query uses the app's socket bridge in both build variants; TCP
snapshots require the SDK's read-only socket option. See
[SDK and launcher updates](#sdk-and-launcher-updates) when updating these services.

### Capture an existing listener

Keep your listener running with `--stats` and the discovery and buffer settings
you want to test. In a **second terminal on the same PC**, run:

```sh
node tools/cd/diagnose-stream.mjs YOUR_PS2_IP --seconds 300
```

This captures the existing audio connection without starting another listener
or opening an audio subscription. Save the listener terminal's output separately:
this mode's bundle contains the packet log and report, not the other terminal's
listener output. Your existing listener keeps running after capture finishes.

### Start a listener and capture together

Stop any existing listener and run this in **one PC terminal**:

```sh
node tools/cd/diagnose-stream.mjs YOUR_PS2_IP --listen --seconds 300
```

`--listen` starts a listener with `--host YOUR_PS2_IP --stats`, opens its audio
subscription and captures TCP headers. It bypasses automatic discovery and uses
the [default PC buffer](#pc-buffering). To test automatic discovery or a custom
buffer such as `--buffer-ms 50`, use [existing-listener capture](#capture-an-existing-listener).

It saves listener output alongside the packet log and
report, and stops the listener it started when capture finishes. The log retains
source timing, underruns, send retries, queue peaks, TCP stall snapshots and
Ethernet counters for wired-versus-Wi-Fi comparisons.

Both capture modes require Linux, `ip`, `tcpdump` and `timeout`, and request sudo
for capture. `--seconds` sets the observation duration. The script prints its
temporary directory and `.tar.gz` bundle path; it changes no network settings.

The report includes packet gaps, repeated sequence ranges, duplicate ACKs, PC
zero receive windows and tcpdump's capture/drop counts. Sequence repeats can
indicate retransmission or reordering; account for dropped capture packets when
interpreting the log. It also shows packets and the last PC ACK around the three
largest gaps.

When the TCP handshake is captured, the analyzer identifies the initial format
record by its stream sequence range and reports the wait until audio separately
from playback gaps. Packet counts still include format traffic. Captures starting
midstream retain their measured gaps and report startup timing as unavailable.

### Read a saved capture

To analyze a previous TCP header log without sudo or another playback session:

```sh
node tools/cd/diagnose-stream.mjs YOUR_PS2_IP --read /path/to/packets.txt
```

Use the PS2 address recorded in that capture. This prints gap details from the
saved log without contacting the console.

### SDK and launcher updates

For changes to the resident network stack or the app's socket bridge, follow the
[SDK rebuild](../sdk/README.md#rebuilds-and-output) and
[service installation](../sdk/README.md#installing-rebuilt-services) workflow.

## Run the recognition service

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
When GnuDB omits a radio-edit suffix or spells a possessive differently, artwork
matching can accept those variants for at least 90% of ten or more ordered
tracks, provided every duration is present and the normal timing limits pass.
Other mix qualifiers remain significant. These artwork variants do not relax
metadata recognition or enable its isolated timing-error exception. Artwork
rejections and download errors are printed by the service and retained in the
report.

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
