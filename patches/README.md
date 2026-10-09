# Dependency patches

<!-- BEGIN TOC -->

## Contents

- [PS2SDK](#ps2sdk)
  - [Socket bridge](#socket-bridge)
  - [Network stack](#network-stack)
  - [Audio refill notifications](#audio-refill-notifications)
  - [Audio cleanup](#audio-cleanup)
- [PS2SDK ports](#ps2sdk-ports)
  - [Selective build](#selective-build)
- [PS2Link](#ps2link)
  - [CD startup](#cd-startup)
  - [Display integration](#display-integration)

<!-- END TOC -->

Patches are applied to dependencies during the build, with one subdirectory per
dependency. Keep patches compatible with the pinned submodules.

## PS2SDK

### Socket bridge

[`socket-rpc.patch`](ps2sdk/socket-rpc.patch) corrects receive-buffer handling and completion state across
the EE/IOP boundary. It also gives the player's bridge a private service identity
and a compatibility handshake, so it can coexist with the launcher's services.
Semaphore creation and deletion failures are reported; failed deletion retains
the handle and blocks reopening until cleanup succeeds.

A compatible release or diagnostic bridge can serve release builds; diagnostic
builds require diagnostic capabilities. See
[network audio startup recovery](../src/common/audio/README.md#startup-recovery) for more details.

The [app build](../cmake/ps2sdk-rpc.cmake) applies this patch to a copy of the
pinned SDK sources and embeds the resulting IOP module in its ELF. It leaves the
submodule and the SDK installation's standard bridge unchanged. Changes to the
patch or copied SDK inputs create a fresh work copy automatically.
See the [bridge host tests](../tests/README.md#socket-rpc-tests) for more details.

### Network stack

[`receive-pool.patch`](ps2sdk/receive-pool.patch) increases network buffering and enables TCP selective
acknowledgements. The purpose is to support concurrent audio and artwork traffic;
it does not guarantee uninterrupted playback. Exact settings belong in the patch.

[`network-diagnostic.patch`](ps2sdk/network-diagnostic.patch) adds observation hooks for diagnostic builds. Its
recorder is maintained in the application's
[IOP diagnostic code](../src/common/platform/README.md#network-diagnostics).

Release and diagnostic SDK both use the capacity
patch; only diagnostics includes the observation hooks.

### Audio refill notifications

[`audsrv-refill-threshold.patch`](ps2sdk/audsrv-refill-threshold.patch) allows free-space callback thresholds up to the
sound ring's full capacity, rather than the upstream half-capacity restriction. It does not resize the ring or alter sound rendering.
Both SDK variants apply it.

The player admits audio when at most 4 KiB remains in audsrv's 20 KiB ring, so its
callback needs a 16 KiB free-space threshold. Upstream accepts only 10 KiB. Using
that threshold would repeatedly notify while our short queue is still too high
for admission. Keep the callback threshold derived from the output policy; do not
increase playback buffering merely to fit the upstream validation limit.

### Audio cleanup

[`audsrv-cleanup.patch`](ps2sdk/audsrv-cleanup.patch) makes sound shutdown report EE and IOP cleanup failures.
It retains unfinished thread, semaphore and RPC resources, preserves completed
steps across retries, and blocks initialization until shutdown finishes. A
successful shutdown resets the client and service so they can initialize again.
Both SDK variants apply it.

## PS2SDK ports

### Selective build

[`select-libraries.patch`](ps2sdk-ports/select-libraries.patch) lets the SDK builder select the ports it needs while
keeping upstream dependency versions and build recipes. It filters downloads,
preparation and builds, and applies only to a temporary source copy.

The selection is defined in the [SDK build script](../tools/sdk/build.sh).
Names refer to upstream repository directories. Include dependencies explicitly;
the selection does not calculate a dependency graph. An empty selection retains
the upstream full build.

## PS2Link

The [launcher builder](../tools/ps2link/build.sh) applies these patches in a
temporary directory. See the [launcher guide](../tools/sdk/README.md#build-the-launcher) for more details.

### CD startup

[`no-cd-stop.patch`](ps2link/no-cd-stop.patch) retains `sceCdInit(1)` to initialize the driver without
requiring a disc, and removes the unconditional startup `sceCdStop()`.

On the tested console, stopping an empty drive left it not ready after later CD
insertion. Disc identification succeeded, but TOC requests failed with error
`00` and zero tracks.

### Display integration

[`display.patch`](ps2link/display.patch) makes the launcher's text screen consume the player's
[display profile](../src/common/platform/graphics/display_config.h). gsKit derives
video timing and display geometry from that profile; text rows, columns and
framebuffer upload stride use its dimensions too.

Both normal startup and exception screens call that implementation.

The launcher uses a single framebuffer for direct text uploads. Its temporary
gsKit setup is released after the initial clear completes. The launcher
[DMA memory adapter](../tools/ps2link/display_memory.c) uses cached, aligned
buffers and flushes them before submission. The EE accelerated uncached mapping
does not cover the low memory used by PS2Link, so gsKit's normal UCAB allocation
and submission routines cannot be used there. Linker wrappers replace those
routines only in the launcher. Host tests check that packet addresses are not
rebased and that chain cache flushing precedes submission.

The upstream SDK text implementation retains its license and copyright notices.
