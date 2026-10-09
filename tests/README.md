# Host tests

<!-- BEGIN TOC -->

## Contents

- [Run the tests](#run-the-tests)
- [Writing tests](#writing-tests)
- [Socket RPC tests](#socket-rpc-tests)

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
`stroom` covers the application, recognition client and companion services;
`benchmark` covers the benchmark application and report tools; `common` covers
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
