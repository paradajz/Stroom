# Host tests

<!-- BEGIN TOC -->

## Contents

- [Run the tests](#run-the-tests)
- [Writing tests](#writing-tests)
- [Socket RPC tests](#socket-rpc-tests)
- [SDK transmit recovery tests](#sdk-transmit-recovery-tests)

<!-- END TOC -->

## Run the tests

From the repository root in the development container:

```sh
make test
```

The suite exercises portable code, host tools and PS2 adapters with hardware
substitutes. C tests use Unity. JavaScript tests run with Node; cover-conversion
tests also need FFmpeg on PATH. The container provides these tools.

Diagnostics are enabled by default. To check release paths separately:

```sh
cmake -S tests -G "Unix Makefiles" -B build/tests-release -DSTROOM_DIAGNOSTICS=OFF
make test BUILD_DIR_TESTS=build/tests-release
```

Run these configurations sequentially because network fixtures share ports.

## Writing tests

Tests under `tests/src` follow the same split as the application sources:
`stroom` covers the application, pad adapter, recognition client and companion
services; `benchmark` covers the benchmark application, framebuffer validation
and report tools; `common` covers
shared audio, MilkDrop, platform, UI and build tools. Shared fixtures belong in
`src/common/support`; SDK substitutes belong in `src/common/stubs` or beside the
adapter tests that use them.

Compile production sources as separate translation units and exercise their
public headers. Arrange inputs through dependency substitutes and check returned
snapshots, callbacks or external operations. Do not include production `.c` files,
extract function bodies, or read or modify private implementation state.

Application runtime tests cover service ordering, individual frame coordination
and cleanup recovery through the [public lifecycle API](../src/README.md#runtime-lifecycle).

Host tests do not establish real-console drive behavior, sound timing, rendering
correctness or performance. Validate those on the PS2.

CD drive tests fill the configured reserve, consume it through a simulated
read interruption, verify sector order across ring wraparound, and retry failed
DMA ranges without losing buffered music. They also cover short tails, seek
discard and the existing cancellation quarantine. Controller tests cover prefill
without false underruns, short playback boundaries, continued output during
read recovery, and the recovery deadline across clock wraparound. Frame tests
check the buffering caption.

The CD reader test runs the real reader and DMA/ring adapters on concurrent host
threads. It blocks SDK read submission, error queries, media probes and TOC reads
while playback drains known sector payloads. It also checks stale completion
rejection on repeated seeks, exact retry ordering, disc removal and cooperative
shutdown while a device call is blocked.

## Socket RPC tests

These host tests compile complete patched EE and IOP bridge sources as separate
translation units, alongside the SDK's socket glue. Hardware, descriptor allocation
and RPC transport use substitutes. IOP startup registers the
request handler through the SDK RPC interface.

Receive tests call `recv()` and `recvfrom()`. They check short reads at every
alignment offset and preserve guards around the destination. After successful
leading/trailing fragment completion, empty and failed reads must leave both the
previous and current buffers untouched. Longer DMA transfers still require console
testing because the SDK's address arithmetic assumes 32-bit pointers.

Compatibility tests cover valid and incompatible bridge replies, unavailable
services and RPC failure. Startup tests check that an incompatible bridge is
rejected before changing network configuration.

Lifecycle tests inject failed semaphore creation and deletion, including reboot
cleanup and the SDK's legacy close entry point. They verify retained handles,
blocked reopening and successful cleanup retries through public APIs.

## SDK transmit recovery tests

The SDK builder runs these checks after applying its network patches, while the
exact pinned lwIP sources are available in its temporary work directory. They
need the host C compiler (`cc`) and do not download additional dependencies.
Use the [SDK rebuild commands](../tools/sdk/README.md#rebuilds-and-output) for
the variant being checked.

The tests compile complete lwIP `api_msg.c` and PS2SDK NETMAN stack sources as
separate translation units, with hardware and allocation substitutes. They check
that full-buffer nonblocking writes retry queued output, preserve partial-write
counts and retain allocation-failure recovery. Stack initialization registers the
real interface and queue callbacks, through which repeated coalescing failures
must leave reference counts unchanged, and successful transmissions must release
their driver-owned reference when dequeued.

Listener restart checks compile lwIP's complete `tcp.c` with the actual SDK
options. They verify that `SO_REUSEADDR` permits rebinding while old CD
connections remain in `TIME_WAIT` or `FIN_WAIT_2`, preserves those connections,
and still rejects conflicts with live sockets that did not enable reuse. Host
socket tests alone miss this failure because Linux already supports the option.

The read-only TCP socket hook is also checked against the pinned lwIP types for
queue sizes, sequence numbers, timer conversion, unsupported requests and state
preservation. The regular CD transport tests check that snapshots are sampled
only during prolonged retries, survive recovery and playback resets, and never
alter streamed samples. PC tests validate diagnostic replies and cancellation.

These SDK checks complement the regular host suite; they run during SDK builds
because lwIP is downloaded and versioned by the SDK's dependency script.
The regular host suite also reproduces the SDK's nested downloader reset and
checks that dependency freezing prevents it; a changed source hash fails the
post-build verification instead of silently producing an unpatched SDK.
