# Diagnostic tools

<!-- BEGIN TOC -->

## Contents

- [Setup](#setup)
- [Query the running console](#query-the-running-console)
- [Record a dropout](#record-a-dropout)
- [Reading diagnostic captures](#reading-diagnostic-captures)
  - [Coverage and session identity](#coverage-and-session-identity)
  - [Locating a stall](#locating-a-stall)
  - [Clock and byte alignment](#clock-and-byte-alignment)
  - [Artwork timings](#artwork-timings)

<!-- END TOC -->

Replace `YOUR_PS2_IP` in the commands below with your console’s IP address.

## Setup

Launch a diagnostic build and leave the app's network receiver active:

```sh
make run PRESET=stroom-diagnostics PS2_IP=YOUR_PS2_IP
```

## Query the running console

With the diagnostic app running, query the console:

```sh
make diagnostics PS2_IP=YOUR_PS2_IP
```

This prints receiver statistics without rebuilding or restarting the app.
Release builds omit these requests. A selected CD closes the AriaCast receiver,
so these queries are not a CD-recognition status interface.

Use `DIAGNOSTIC=artwork`, `DIAGNOSTIC=metadata`, or `DIAGNOSTIC=driver` to inspect
cover handling, the last metadata request, or available resident driver counters.
The driver query can report unavailable when the running stack lacks support.

Collect cover diagnostics before changing tracks, and compare counters around
an event rather than treating cumulative totals as evidence of a dropout.
Diagnostic traffic is intended for a trusted local network and is unauthenticated.

## Record a dropout

When using PS2Link, install the **diagnostic variant** of the [patched launcher](../sdk/README.md#build-the-launcher) and fully
restart the console to enable IOP diagnostic recording. Then
[launch the diagnostic app](#setup).

After a dropout, leave playback running for at least two seconds, then collect:

```sh
make diagnostic PS2_IP=YOUR_PS2_IP
```

JSON captures are saved under `build/diagnostics` by default; override
`BUILD_DIR_DIAGNOSTICS` to change this. The command retrieves existing recordings
without restarting playback. Collect after the event because retrieval adds
network traffic.

Capture slots are bounded and retained without replacement. After saving them,
free the slots for the next attempt:

```sh
make diagnostic-clear PS2_IP=YOUR_PS2_IP
```

Do not reboot or clear events during retrieval. Interrupted or inconsistent
downloads fail instead of being saved as complete captures.

Captures target five seconds before and two seconds after a suspected output
shortage or stall. Capacity limits and session changes can shorten that window;
a visual freeze alone may not trigger a capture. For cover freezes, query
[artwork diagnostics](#query-the-running-console) immediately afterward.

## Reading diagnostic captures

Numeric event IDs, record fields and shared storage limits are defined in the
[diagnostic contract](../../shared/contracts/diagnostic.json). The client decodes
records into named fields.

### Coverage and session identity

The session token identifies a stream generation, not a connection count. Group
records by equality within one receiver lifetime. A source change finishes a
pending event early and clears rolling history; saved events remain until cleared.
Full capture slots do not overwrite earlier events.

Triggers identify accepted silence during a shortage or an extended gap between
accepted sound submissions. They indicate a suspected output problem, not proof
of an audible hardware underrun.

IOP observations describe activity inside the PS2 network stack, after Ethernet
reception; they are not a wire capture. A successful IP output submission does
not prove transmission or receipt by the sender.

Before interpreting missing activity, inspect:

- `historyLimited`: rolling capacity shortened the pre-trigger window.
- `truncated`: records did not fit in the saved event.
- `iopLost`: IOP observations were overwritten before collection.
- `missed`: no free slot was available for another event.

A successful IOP status response alone does not establish useful recording.
Confirm that `iopRecords` increases during playback. Missing diagnostic support,
a failed socket selection or an incompatible launcher can leave EE observations
available without the corresponding IOP timeline.

### Locating a stall

Compare successive queue samples and cumulative byte/message counters. Socket
read durations include the EE/IOP bridge call. PCM byte counts exclude WebSocket
framing, so they are not interchangeable with socket byte counts.

TCP observations follow the selected audio connection, rather than artwork or
control connections. Pool and input-queue failures are global observations while
recording is selected.

SACK state describes support and negotiation. Saved events must be interpreted
using their own state observations; the manifest describes the current session.
SACK blocks describe received byte ranges, with exclusive right edges, rather
than missing ranges. Correlate transmit IDs within their session and nearby
timestamps, checking the IP submission result and option-validity flag.

If the receive window stays open but packets do not reach the TCP hook, the
capture alone cannot distinguish sender scheduling, network loss or rejection
before that hook. An external packet capture may be needed.

Output observations distinguish queue queries, submissions and waiting for the
output lock. Local busy rejections are not necessarily sound RPCs; see
[output notifications](../../src/common/platform/README.md#sound-output). Queued bytes describe
the sound service's software ring, not the precise audible playback boundary.
Worker-stage timings help separate socket, playback, artwork and scheduling delays.

### Clock and byte alignment

EE and IOP clocks have different origins. Clock observations bracket the
synchronizing RPC; the client maps IOP timestamps using its EE midpoint. The
RPC bracket limits alignment precision. Records arrive in batches and may be
out of timestamp order.

`byteBase` is the EE socket byte count at IOP selection. Add it to IOP read counts
when comparing with EE reads. The socket mailbox can already contain data then,
so compare delivery deltas instead of assuming its initial delivery total equals
the read count. Timestamps, sequence numbers and counters can wrap. Signed errors
are stored in unsigned words and need signed interpretation.

### Artwork timings

Cover observations describe downloading, decoding, allocation and texture
upload. Phase IDs, readable names and descriptions are defined in the
`artworkPhases` section of the
[diagnostic contract](../../shared/contracts/diagnostic.json).
An observation's timestamp is its end; subtract its duration with wrapping
arithmetic to find the start. Durations include preemption and waits, not just
exclusive CPU execution.

Decode and compressed-buffer release run on the background worker. Texture
allocation and upload run on the renderer. See
[artwork scheduling](../../src/common/audio/artwork/README.md#scheduling).
A long decode alone does not demonstrate an
audio interruption; correlate output, queue and worker observations.

UI results must match the published artwork identity. Mailbox collection can
place older timestamps after newer records. Frame presentation observations are
no longer emitted, and the artwork reply's `display` field remains empty.

The [artwork query](#query-the-running-console)'s `coverTiming` retains
per-phase counts, maxima and the last operation for the latest observed revision.
It resets at session boundaries; a revision can remain visible until another
cover operation occurs. Collect after the event because diagnostic replies add
network traffic.
