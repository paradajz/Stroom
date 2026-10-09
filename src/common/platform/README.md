# PS2 platform adapters

<!-- BEGIN TOC -->

## Contents

- [Sound output](#sound-output)
- [CD drive](#cd-drive)
- [Networking](#networking)
- [Socket RPC bridge](#socket-rpc-bridge)
- [Network diagnostics](#network-diagnostics)
- [Threads](#threads)
- [Display](#display)
  - [Configuration and timing](#configuration-and-timing)
  - [Resource lifecycle](#resource-lifecycle)

<!-- END TOC -->

Platform adapters isolate hardware and SDK details from playback, visualization
and UI policy. They provide controller input, timing, display setup, thread
scheduling and IOP service access.

Public adapter functions use the `platform_` prefix. Cleanup follows the
[shared cleanup rules](../../README.md#cleanup-rules).

`platform_open()` initializes RPC, startup text output and application thread
scheduling before device adapters open. After device owners close,
`platform_close()` finishes retained network cleanup and
restores scheduling. The [clock adapter](time/clock.h) provides milliseconds, raw
timer ticks and conversion to microseconds.
The [sleep adapter](time/sleep.h) suspends the calling thread in microseconds.

The main application runs on the EE; device services run on the IOP. Startup
reuses resident services rather than resetting the IOP. The scheduler leaves
room for sound callbacks to run while the application renders.

The [memory adapter](memory/cache.h) provides EE data-cache writeback for hardware visibility.

Host tests use SDK substitutes. They check adapter behavior but do not replace
compilation with the PS2 SDK or hardware testing.

## Sound output

The adapter owns sound-driver/service loading, RPC binding, format and volume
setup, device synchronization, ring preparation and PCM submission. The embedded
`freesd` and `audsrv` modules and SDK linkage belong to the platform target.

Producer tokens are opaque: the caller chooses the permitted producer. The adapter
serializes hardware access and checks those tokens during preparation and writes,
so a source change cannot let a previous producer overwrite or stop new sound.
The [audio output interface](../audio/README.md#shared-sound-output) assigns source identities
and carries playback policy.

Preparation overwrites the hardware ring while muted, with a one-second deadline.
Writes accept stereo PCM16 at the shared 48 kHz rate. Busy submissions leave PCM
with the caller for retry. Requested volume changes are applied by workers under
the device lock.

During playback, audsrv refill callbacks invalidate a shared queue snapshot.
Workers verify the queue on notification and account for successful writes locally;
busy retries and CD position reads reuse that snapshot. The snapshot can overstate
remaining audio until the next callback. Network listening bypasses sound output.
The callback only sets a flag: it never locks or performs sound RPCs. Repeated
notifications coalesce, including notifications received during a queue query.
Mute changes still apply while a producer is waiting for space.

Stopping audsrv disables its threshold, so preparation re-arms notifications and
invalidates the snapshot. The first submission queries directly without depending
on a callback. Preparation retains bounded polling while overwriting stale audio. Callback registration failure fails preparation
rather than silently starting playback without notifications.

Diagnostic busy-write counts include local rejections; they are not sound-RPC
counts. Queue observations are recorded only when a hardware query occurs.

Close checks sound stop, service shutdown and semaphore deletion in order. Diagnostic
builds log the failed operation and SDK return code.

## CD drive

The drive adapter owns CDVD module loading, RPC availability checks and
no-disc initialization. It exposes media probes, raw track tables and sector
reads without depending on audio status or playback types. Startup progress and
diagnostic storage come from the caller.

The [CD audio worker](../audio/README.md#cd-playback) owns the adapter's lifetime. Its
controller validates track tables and decides detection retries and playback.

Sector reads use two static DMA banks. A stalled read triggers cancellation
after five seconds. If cancellation does not finish within another second,
the banks stay quarantined until console restart, including across adapter
reopens. These deadlines cannot interrupt an SDK call that itself blocks.

## Networking

This layer starts or reuses Ethernet services and manages access to the shared
socket client. Existing resident services and IP configuration are preserved;
an unconfigured interface uses the configured static addresses or requests DHCP.
The last owner's close performs client cleanup. Unfinished startup
rollback can also be retried at final platform shutdown.

It loads the app's socket bridge and verifies compatibility before using sockets. The bridge is distinct from the resident Ethernet stack.
The app embeds the modules it needs for startup. Without resident networking,
it loads DEV9, NETMAN, SMAP and the NETMAN TCP/IP stack before the socket bridge.
This path avoids the legacy SMAP driver's copied lwIP structures.

AriaCast and optional CD recognition use these services. Audio sessions and
metadata policy belong to their consumers, not this layer.

The socket-mode adapter enables nonblocking operations for AriaCast, CD lookup
and artwork connections. It translates the SDK's negative return code into
`errno`; callers retain retry and connection cleanup policy.

See [network audio startup recovery](../audio/README.md#startup-recovery).

## Socket RPC bridge

This adapter connects EE socket callers to the embedded IOP service. See the
[socket bridge patch](../../../patches/README.md#socket-bridge).

Diagnostic builds can query optional resident driver statistics. This does not
load a new NETMAN service or reconfigure the Ethernet stack. Queries share the
socket RPC lock.

## Network diagnostics

These sources implement the recorder used by the
[network diagnostic patch](../../../patches/README.md#network-stack).
The app's socket bridge queries its observations; a stack without the diagnostic
extension reports them unavailable.

Recording observes network activity without changing transport policy.
Wire definitions shared with host tools belong in the
[diagnostic contract](../../../shared/contracts/diagnostic.json).

These sources are compiled into the SDK as well as used by the app. After edits,
follow the diagnostic [SDK rebuild and installation workflow](../../../tools/sdk/README.md#rebuilds-and-output).
See the [diagnostic guide](../../../tools/diagnostic/README.md).

## Threads

The worker helper creates a mailbox lock, a completion semaphore and a prioritized
EE thread. It attempts to release resources created before a startup failure and
retains the SDK operation and error code for subsystem diagnostics.

Event-driven workers can request a coalescing wake semaphore. Their loops wait
for notifications, and close signals the semaphore so an idle worker can stop.
Notification storage is released with the other synchronization resources.

Each subsystem supplies its entry point, aligned stack, argument and priority.
It owns its loop, commands, state and startup order. Workers stop cooperatively
through the lifetime flag and call `platform_worker_finish()` after subsystem cleanup.

Close requests stop and checks thread status once. An active thread leaves
cleanup pending without waiting; only a dormant thread can be deleted and its
synchronization released. Diagnostic builds log the failed SDK operation, resource identifier and return
code. A blocked worker can prevent shutdown from completing, but close does not
wait for it. SDK inspection and deletion calls remain synchronous.

Opening rejects any allocated worker resources without modifying them. The
caller-supplied stack remains borrowed until close succeeds.

The scheduler adapter manages application-thread priority; worker priorities
remain with their callers. Background workers can ask the scheduler for one
priority level below the calling renderer without changing its priority.

## Display

### Configuration and timing

The [display profile](graphics/display_config.h) owns the hardware mode and framebuffer
geometry, and imports refresh timing from the
[shared display contract](../../../shared/contracts/display.json).
The [PS2Link build](../../../tools/sdk/README.md#display-configuration)
also consumes it. Display setup, UI/MilkDrop geometry and benchmark
budgets consume that profile. Changing the physical output mode requires a
rebuild; there is no runtime video-mode selector.

The `display` startup preference and the Visualiser menu's Display option only
switch between player panels and fullscreen visualization. They do not change
resolution, refresh rate or sync type. See [startup configuration](../../../README.md#startup-configuration) for more details.

The current refresh interval is approximately 16.68 ms. The menu's
[frame-rate cap](../ui/README.md#frame-rate) presents no faster than
every second refresh at 30, or every refresh at 60: approximately 29.97 or
59.94 FPS. Rendering and background work can take longer; missed refreshes do
not trigger catch-up frames.

The app sleeps on a semaphore while waiting for its presentation refresh. The
vertical-blank interrupt wakes the renderer. Buffer presentation does not
perform another refresh wait. GS-completion waits remain unchanged.

### Resource lifecycle

The [frame renderer](../ui/frame/frame.h) owns the display context. The platform
adapter initializes GS and DMA, submits and completes graphics work, and handles
refresh waits and buffer presentation. UI chooses the refresh interval passed
to the adapter. On shutdown it removes the vertical-blank handler before
releasing its semaphore and GS context. The GS context stays allocated while
semaphore deletion is unfinished. Diagnostic builds log the
failed deletion. UI composition, textures and scene drawing remain with the renderer.

A failed display close retains the frame's graphics context and scene caches,
and reopening is blocked until cleanup succeeds. After successful display
shutdown, frame cleanup releases scene caches and color-curve CPU packets and
forgets texture VRAM addresses. Reopening reserves fresh VRAM and starts with
empty feedback history.

The [readback adapter](graphics/readback.h) provides synchronous framebuffer strips for
GPU validation; test patterns and pixel comparisons stay in the benchmark.
