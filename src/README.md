# Source architecture

<!-- BEGIN TOC -->

## Contents

- [Stroom](#stroom)
- [Preset benchmark](#preset-benchmark)
- [Shared libraries](#shared-libraries)
  - [Portable utilities](#portable-utilities)
- [Return values](#return-values)
- [Cleanup rules](#cleanup-rules)
- [Runtime lifecycle](#runtime-lifecycle)
- [Frame and worker flow](#frame-and-worker-flow)

<!-- END TOC -->

The source tree contains two independent applications and their shared libraries.
Each app has its own executable target and entry point.

## Stroom

The player starts at [stroom/main.c](stroom/main.c). Its
[application runtime](stroom/app/runtime.h) owns startup, failure cleanup and
frame processing. The controller keeps playback, visualization and menu state
together and returns actions for the runtime to dispatch. Drawing and device
operations belong to their own subsystems. The optional CD recognition client
lives in `stroom/recognition`.

The application applies [startup configuration](../README.md#startup-configuration)
before starting network and audio services. The runtime and artwork adapter are
linked into the executable separately from the portable controller and
configuration library.

## Preset benchmark

The benchmark starts at [benchmark/main.c](benchmark/main.c) and owns its
[measurement runtime](benchmark/runtime.h), timing aggregation and colour-curve
GPU checks. It uses shared audio and rendering directly, independently of
Stroom's controller, startup configuration and recognition client.

See the [benchmark guide](../tools/milkdrop/README.md#run-a-benchmark).

## Shared libraries

Stroom and the preset benchmark share these modules so measurements exercise
the same audio, equation and rendering implementations as playback:

- [Audio](common/audio/README.md)
- [MilkDrop](common/milkdrop/README.md)
- [UI](common/ui/README.md)
- [Platform](common/platform/README.md)
- `common/profiling`: shared measurement hooks, enabled by benchmark builds.

Each screen owns its controller, view and private layout. `ui/frame` composes
views and animates shared panels. Controllers return actions; views do not issue
playback commands. Shared widgets belong in `ui/shared`, not in another screen.
The [frame renderer](common/ui/frame/frame.h) composes the frame and presents
through the [platform display adapter](common/platform/README.md#display). Both
applications use this path; the benchmark reads its shared profiling hooks.

Portable contracts live below their consumers: button state in `platform/input`,
track metadata in `audio/common`, and disc format in `audio/cd/cd_format.h`.
CD recognition and AriaCast share metadata representation and JSON parsing; each
owns its request and session handling. Keep transport instrumentation with
transport and rendering instrumentation with UI.

### Portable utilities

`common/util` contains shared helpers with no hardware or SDK dependency. Public
utility functions use the `util_` prefix.

`STROOM_LOG(format, ...)` in [diagnostics.h](common/util/diagnostics.h) adds the
`stroom: ` prefix and a trailing newline. Callers supply only the message and
arguments. With diagnostics disabled, the macro produces no output and does not
evaluate its arguments.

## Return values

Operation results use `int`: `0` means success, a negative value means failure,
and a positive value means pending, busy or temporarily unavailable. Callers check
`result == 0` before consuming a result. Each API documents its intermediate states.

Fallible APIs declare named negative errors in their own headers. Values are
local to an API, so compare against that API's constants rather than literal
numbers or another module's enum. Callers that only need success/failure use
`== 0` or `< 0`; callers that need a particular failure use its named constant.
Source-specific recovery belongs to [audio](common/audio/README.md).

Subsystem wrappers translate lower-level failures into their own error codes.
Adapters that deliberately pass failures through expose aliases, as the audio
output adapter does for platform output errors. Raw SDK codes and explanatory
messages remain available through existing diagnostics. These enums describe
failures; they do not introduce a shared recovery manager or retry policy.

Predicates, value queries (such as activity, byte counts and track numbers),
pointer-returning APIs and SDK callbacks retain their documented contracts.

## Cleanup rules

Resource-owning lifecycle APIs retain ownership until cleanup returns `0`.
A positive result means cleanup is pending; a negative result reports failure.
Both leave remaining resources owned by the caller, which must retry close.

Failed startup can leave partially acquired resources. Finish cleanup before
reopening, resetting state or reusing owned storage. Release resources in
dependency order; completed cleanup steps are preserved across retries, and
later attempts handle only unfinished steps. An SDK release failure must not
discard the resource's ownership record.

Subsystem guides describe their cleanup order, pending conditions and recovery
policy; their API headers document initialization requirements and error codes.

## Runtime lifecycle

The [runtime API](stroom/app/runtime.h) separates startup, one frame and one
cleanup attempt. Its state is zero-initialized before first use. Closing stops
frame execution immediately and keeps graphics and platform services until
their dependants have stopped. Active workers leave cleanup pending.

Audio startup failure does not prevent the UI from opening. Failure to create
the shared output lock disables audio until console restart. The runtime retains audio
cleanup ownership, and a source close/open cycle does not retry the failed lock.
Sound-driver initialization still happens later in the workers.

`stroom/main.c` opens the runtime and repeatedly executes one frame. If startup
fails, it retries runtime cleanup with a short sleep between attempts. Host tests
call these public lifecycle operations directly to verify coordination and
failure recovery. After five seconds of unfinished startup cleanup, `main.c`
reports the condition to stderr once and continues trying; it does not force
resource release. During normal frame execution, unfinished audio cleanup
retains ownership while later frames retry it.

## Frame and worker flow

Each Stroom frame polls sources and input, updates application and visualization state,
dispatches playback requests, [prepares artwork](common/audio/artwork/README.md),
then composes and submits graphics. MilkDrop
continues behind player/settings screens.
Playback dispatch uses the [audio-owned transport contract](common/audio/README.md#transport-requests).

Input is routed to the active screen or menu. CD play/pause and sound toggling
are handled globally. One input frame has one owner: opening settings or changing
screens consumes that action rather than also applying it to the newly selected
screen.

The controller resets player navigation, elapsed/remaining-time selection and
visualization audio history on source or disc changes, and closes the menu.
Network session changes also reset audio history. The current display setting,
preset state and visualization clock are preserved across these transitions.

Audio workers exchange copied commands and snapshots under semaphores. The
CD, network-audio and CD-recognition workers use the
[platform worker helper](common/platform/README.md#threads) for thread and semaphore
lifecycle; their loops and startup order remain in their owning subsystems. The
[source coordinator](common/audio/README.md) grants sound output to one producer at a
time. CD commands carry a disc generation so they cannot affect a replacement disc.

Optional [CD recognition](../tools/cd/README.md) has a separate worker and can
initialize networking before audio-source detection starts. It exchanges disc
layouts and metadata with the PC service independently of playback. Recognition
and network audio share [platform networking](common/platform/README.md#networking);
closing the audio receiver does not shut down a recognition client's connection.
An unavailable recognition service leaves CD playback available.
