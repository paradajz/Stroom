# Audio

<!-- BEGIN TOC -->

## Contents

- [Source selection and recovery](#source-selection-and-recovery)
- [Transport requests](#transport-requests)
- [CD playback](#cd-playback)
  - [Detection and recovery](#detection-and-recovery)
    - [Restart-required drive failures](#restart-required-drive-failures)
  - [Optional recognition](#optional-recognition)
  - [Outgoing CD audio](#outgoing-cd-audio)
- [Network audio](#network-audio)
  - [Startup recovery](#startup-recovery)
  - [AriaCast receiver](#ariacast-receiver)
    - [Supported transport](#supported-transport)
    - [Buffering and failures](#buffering-and-failures)
    - [Listening extension](#listening-extension)
    - [Stream completion](#stream-completion)
- [Shared sound output](#shared-sound-output)
- [Validation](#validation)

<!-- END TOC -->

Audio sources provide a common snapshot for visualization. The source coordinator
selects CD or network playback and grants access to shared sound output; the two
sources are not mixed.

## Source selection and recovery

Failed shared sound-output startup prevents source detection until a subsequent
open succeeds. Restart-required failures remain latched across open attempts.

Source changes wait for worker cleanup to succeed before selecting playback.
Pending source changes show the neutral waiting screen and retry worker cleanup
on later polls with no sound producer selected. Cleanup failures show a generic
error. Failed CD or network startup also reports a generic error while
cleanup is retried; another start requires successful cleanup and a one-second
retry interval.
For CD, this includes asynchronous drive initialization failures that stop its
worker. Once that worker closes, network playback remains available while CD
startup retries on the waiting screen.

Fatal network receiver errors use the same cleanup and restart path. Recovery
clears stale audio and metadata; normal stream endings keep the receiver available.

Shutdown closes shared output after both workers have joined, following the
[shared cleanup rules](../../README.md#cleanup-rules). See the
[worker lifecycle](../platform/README.md#threads) for more details.

At startup, the app checks for an audio CD. While a disc is selected, the network
audio receiver is closed. Without a disc, the app waits for network audio and
continues disc detection. An active stream keeps control until it ends.
Optional CD recognition can use networking independently of the audio receiver.

CD and audible network playback feed both sound and analysis. A negotiated
listening session feeds analysis only. Source changes clear old analysis data;
MilkDrop does not need to know which source supplied it.

Audio buffers, analysis snapshots and metadata representation live in `common`.
Metadata storage and comparison are separate from metadata JSON parsing. CD
recognition and AriaCast share these facilities, while retaining their own
request handling and artwork identity rules.

Workers publish status and samples for the main thread. Hardware operations stay
with the owning backend; UI controls submit requests rather than accessing the
drive or sound device directly.

## Transport requests

[Shared transport requests](common/transport.h) describe ordered commands,
optional track programs and held scan direction independently of a source.
Controllers return this audio-owned type; the application runtime forwards it through
[audio_source_apply](source/source.h) using the snapshot from that update.
Shared output mute is forwarded through [audio_source_set_muted](source/source.h)
separately from transport.

The source coordinator routes requests to the selected backend. CD supports the
current operations and validates the snapshot's disc generation. Network playback
remains sender-controlled and ignores local transport requests; detecting also
ignores them. Future Aria transport support can implement the same contract
behind this dispatch boundary.

## CD playback

The CD backend detects audio discs, reads their track layout, resamples audio for
the shared output device, and publishes playback status. Once drive and output
preparation succeed, a newly loaded disc follows the autoplay preference in
[startup configuration](../../../README.md#startup-configuration).

The playback worker decides playback order and recovery and submits sound.
A separate [platform reader worker](../platform/README.md#cd-drive) owns CDVD
operations and supplies completed sectors independently of sound submission.

CD playback starts after the short prefill configured by
[PS2_CD_PREFILL_MS](../platform/cd/drive.h), capped by the available ring capacity
and remaining audio. This applies to initial playback, track skips, resume and
scan release. The reader keeps filling the larger reserve while music plays.
[PS2_CD_RESERVE_SECONDS](../platform/cd/drive.h) defines that reserve's capacity;
sector count, memory use and the read-recovery window derive from it. Protection
against drive interruptions grows as the reserve fills after a seek.
The UI reports `CD / BUFFERING` only during the short prefill. Pause, seek, stop
and disc changes discard the old reserve; resume and track jumps prefill again.
Continuous track boundaries retain read-ahead.
The backend accepts the [shared transport requests](#transport-requests)
through [cd_transport_apply](cd/cd_transport.h); batch submission preserves the
existing per-operation locking and is not atomic.

### Detection and recovery

For launcher-related late-insertion failures, see
[CD startup](../../../patches/README.md#cd-startup).

Only audio CDs are supported. Track layouts are validated before playback;
data and mixed-mode discs are not supported. Detection retries while a newly
inserted disc settles. Play can retry failed preparation.

After a failed sector read, the reader requests a fresh media check. Removal or replacement
clears the old playback state instead of reporting an error from the previous
disc. With the same disc present, the [reader](../platform/cd/reader.c) schedules
retries while buffered audio keeps playing. The controller continues publishing
read boundaries and enforces the overall recovery window. Successful read
completion ends recovery; exhausting the configured recovery
window reports `CD READ ERROR - PRESS PLAY TO RETRY`. If the
reserve empties first, local output receives silence until reads recover; no
unread sectors are skipped. Sound output failures and drive quarantine still
stop playback immediately.

Track changes, pause and stop clear old analysis samples. Held scanning is muted;
releasing the button resumes from the selected position while preserving whether
playback was playing, paused or stopped. Reported position is estimated from
submitted and queued audio, not a hardware playback timestamp.

#### Restart-required drive failures

A worker snapshot reporting drive quarantine makes CD polling return
`CD_ERROR_RESTART_REQUIRED`. Source coordination closes the worker and stops CD
startup retries for the rest of the application run, leaving network playback
available. Closing and reopening audio sources does not clear this state.

### Optional recognition

[CD recognition](../../../tools/cd/README.md#run-the-recognition-service) sends the track layout to a PC
service for labels and cover art. It is independent of drive playback: a missing
service or unsuccessful lookup leaves playback available.

### Outgoing CD audio

The optional CD stream publishes successful 48 kHz resampler output through a
separate worker. CD callbacks only copy bounded blocks; socket work never runs
on the playback worker. Local volume changes do not alter outgoing PCM. Streaming
is available automatically while CD is selected and does not mute local output.

The application polls streaming lifecycle independently of the incoming AriaCast
receiver. It owns a reference to platform networking while CD is selected, including
alongside recognition. Startup and socket failures leave CD playback available;
cleanup completes before retrying. Incompatible resident modules stop streaming
startup retries until the application restarts.

UDP discovery and TCP subscriptions use the [CD stream contract](../../../shared/contracts/cd_stream.json).
Each connection sends the subscription request, receives a format record, then
PCM records. Every record has four-byte magic, a big-endian uint32 type and a
big-endian uint32 payload size. PCM stays little-endian; format fields are
big-endian. An empty END record distinguishes normal completion from a cancelled
session. During startup, new clients receive the opening from the existing ring
while it remains intact. After the ring wraps or playback ends, they start at
the current stream cursor. The transport is a Stroom extension, not an AriaCast
sender.

Worker and transport queues each retain at most 128 resampled sectors (about
1.7 seconds of headroom, without deliberate prebuffering). Temporary socket
buffer exhaustion retries from the current partial-write offset; a send with no
progress for 1.5 seconds disconnects the affected client. Each client
tracks its own cursor and partial-write offset. A lagging or stalled connection
is disconnected; a worker-mailbox overflow cancels stale sessions. Pause, seek,
stop, scan and errors cancel outgoing sessions. Automatic Repeat, Shuffle and
Program transitions retain the outgoing connection and queued PCM while the
local reader and sound output prepare the next track. END is sent only when
playback has no following track, allowing queued PCM to drain. Discovery replies
expose source and transport observations independently of audio subscriptions.

The [CD tools guide](../../../tools/cd/README.md) covers listener setup,
[PC buffering](../../../tools/cd/README.md#pc-buffering) and
[counter interpretation](../../../tools/cd/README.md#timing-and-counters).

## Network audio

The network backend receives AriaCast sessions and publishes audio, metadata and
downloaded artwork. Session changes clear old sample history so a
reconnection is visible even if the main thread missed the idle interval.

The AriaCast receiver owns discovery, client sessions and playback
buffering. [Platform networking](../platform/README.md#networking) owns Ethernet
startup, addressing and the socket bridge. See
[startup configuration](../../../README.md#startup-configuration) for more details.

A handshake alone does not claim the audio source: PCM must arrive.

The app embeds its required network modules. It does not need separate network
IRX files alongside the ELF. A network startup failure is shown in the UI and
does not make CD playback depend on a working network.

### Startup recovery

An incompatible resident network module reports `NETWORK_START_RESTART_REQUIRED`:
cleanup is retried as needed, but receiver startup stops for the remainder
of the application run. The UI retains the console-restart instruction whenever
no CD is selected. Closing and reopening audio sources does not clear this state.

### AriaCast receiver

Select **Stroom** in an AriaCast sender or connect to the console's displayed
address. Audio uses TCP 12889; discovery uses UDP 12888 and needs a reachable
broadcast network. The [PC sender](../../../tools/aria/README.md) also works
with this receiver.

#### Supported transport

The receiver implements discovery and the `/audio`, `/stats`, `/control` and
`/metadata` endpoints. Audio is raw stereo PCM; format and protocol constants
belong in the [shared contracts](../../../shared/README.md).
Only one audio sender is active at a time.

`/stats` reports stream counters. Its `droppedFrames` counts older PCM packets
skipped in listening mode and resets at each audio connection. `/control` accepts
a connection but does not implement remote transport or volume commands. Control playback on the sender;
the PS2's local mute does not pause the incoming stream.

Metadata can arrive through HTTP POST or a metadata WebSocket. Supported labels
are title, artist, album and artwork URL. Omitted fields retain their values;
null clears a field. Malformed or duplicate label fields reject the update.
Updates belong to the active audio sender; disconnect clears its labels and artwork
even while buffered audio finishes playing. Metadata sent before the next audio
connection remains subject to the existing sender-address and freshness checks.
Timing fields are not used. See [shared artwork transport](artwork/README.md) for more details.

This is a local-network receiver without authentication or encryption. It does
not provide mDNS discovery, video, a web dashboard or WAV rebroadcasting.

#### Buffering and failures

Audible playback prebuffers audio and pauses socket reads when its queue is full.
Backpressure preserves queued audio instead of discarding old messages to catch
up, so a stall can add latency. Short gaps can recover within the same session;
long gaps close it. Queue underruns feed silence rather than replaying stale sound.

Stream generation identifies session changes, not a connection count. Consumers
must compare it for change, without relying on its numeric difference.

#### Listening extension

The receiver advertises listening support in its READY response. A sender may
request `STROOM LISTEN`, optionally followed by a space and a device name, before
sending audio. It must wait for `STROOM LISTENING` before sending PCM.

Listening supplies visualization input without PS2 sound or sound prebuffering.
Consumption follows the message clock; after a delivery burst it skips older
queued packets and analyzes the newest one. The clock restarts at that consumption
time, so recovery does not rush through old visualization frames.
A late request is rejected; changing mode requires a new connection. New sessions
default to audible playback. The device name is a display label, not track
metadata. Its size limit comes from the shared metadata contract.

This extension works in both release and diagnostic builds. It is separate from
AriaCast volume commands.

#### Stream completion

When TCP or the audio WebSocket closes, or the PCM receive timeout expires,
audible playback drains complete PCM packets already queued before stopping. This
also releases short clips and queues below the normal prebuffer threshold.
No sender extension is required. Incomplete packets cannot be played.

The receiver uses the shared [sound lead-out](#shared-sound-output) before
stopping output. Listening mode analyzes its newest remaining packet and waits
through its final interval without sound RPCs. Draining has a bounded deadline so
failed output cannot hold the session indefinitely. A new audio session replaces the old queue; source changes,
receiver shutdown, malformed input and sound failures cancel immediately.

## Shared sound output

This module is the audio-facing interface to the
[platform sound adapter](../platform/README.md#sound-output). CD and network playback
use the same output path; resampling and source-specific buffering remain with
their backends.

The source coordinator selects the permitted producer and forwards the requested
mute state. The interface maps CD/network identities to opaque platform producer
tokens. Platform setup is reused rather than reset for every source change.

CD lead-out and network-stream draining share a silent-tail policy.
After all PCM has been submitted, successful silence writes give queued audio
time to play and prevent stale audsrv ring contents from repeating before stop.
Busy writes do not advance this tail. Source cancellation and receiver shutdown
still stop immediately.

## Validation

Host tests cover playback policy, disc changes, retries, resampling, framing,
session ownership, timeouts, backpressure, reconnection and mocked drive/output
failures. Validate real drive behavior, sound quality and timing on the console.
