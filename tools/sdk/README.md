# Build tools

<!-- BEGIN TOC -->

## Contents

- [Container tools](#container-tools)
- [Building the application](#building-the-application)
  - [Video mode](#video-mode)
  - [Output directories](#output-directories)
- [Build the SDK](#build-the-sdk)
- [Rebuilds and output](#rebuilds-and-output)
- [Build the launcher](#build-the-launcher)
  - [Display configuration](#display-configuration)
- [Installing rebuilt services](#installing-rebuilt-services)

<!-- END TOC -->

Run Make commands from the repository root in the development container.

## Container tools

The [development container](../../.devcontainer/Dockerfile) provides the compiler
toolchains, Node.js, FFmpeg and 7-Zip. After changing container dependencies, use
VS Code's **Dev Containers: Rebuild Container** command.

## Building the application

Build the default player:

```sh
make
```

`PRESET` selects the CMake configuration and matching SDK. The default is `stroom`;
`stroom-diagnostics` enables inspection and recording. See
[CMakePresets.json](../../src/CMakePresets.json) for configuration values.

To configure the app and prepare the SDK without building the app:

```sh
make configure-app
```

For memory-card packages and installation, use the
[console setup guide](../../setup-manual.md).

### Video mode

`VIDEO_MODE` selects compile-time video output for the app and PS2Link:

| Setting                     | Output                                                 | Connection                                            |
| --------------------------- | ------------------------------------------------------ | ----------------------------------------------------- |
| `VIDEO_MODE=480i` (default) | 640×480 interlaced NTSC, about 59.94 fields per second | Composite, component or RGB; display must accept NTSC |
| `VIDEO_MODE=480p`           | 640×480 progressive, about 59.94 Hz                    | Compatible component or RGB display                   |

For progressive development builds, first package the launcher:

```sh
make dev VIDEO_MODE=480p
```

[Install](../../setup-manual.md) and boot that launcher, then launch the app in
the same mode:

```sh
make run VIDEO_MODE=480p PS2_IP=YOUR_PS2_IP
```

Pass `VIDEO_MODE=480p` to package and benchmark commands as needed too.
Each Make invocation defaults to `480i`
unless specified; switching modes rebuilds the affected app files automatically.
An SDK rebuild is unnecessary. Direct CMake configuration uses
`-DSTROOM_VIDEO_MODE=480i` or `-DSTROOM_VIDEO_MODE=480p`.

### Output directories

Build outputs follow `build/<CMAKE_PRESET>/<VIDEO_MODE>/`; `BUILD_DIR_APP` can
select a different app build directory. For example:

```text
build/stroom/480i/app/stroom-packed.elf
build/stroom/480i/app/memcard/
build/stroom/480i/ps2link/PS2LINK.ELF
build/stroom/480i/ps2link/memcard/
build/stroom/480i/tests/
build/stroom/480i/lint/
build/stroom/480i/diagnostics/
```

The same layout applies to `build/stroom-diagnostics/480i/` and to each preset's
`480p/` directory. Packages, binaries, test builds, lint outputs and captures are
separate for each configuration. Each app or launcher package goes in `memcard/`
beside its executable. Building one leaves the others intact.
The [one-time installer](../../setup-manual.md#build-and-install) goes in
the fixed `BUILD_DIR_INSTALLER` directory, `build/installer/`, outside the preset
and video-mode directories.

SDK installations in `build/sdk/` and downloaded archives in `build/downloads/`
are dependency caches and remain reusable. Release and diagnostic SDKs have
separate installations; `PRESET` selects the correct one.

App builds produce both `stroom.elf` for PS2Link and a compressed `stroom-packed.elf`
for memory-card boot in that build directory. Packing runs again when the app
changes or `stroom-packed.elf` is missing. The production package uses the packed
ELF; PS2Link launches the unpacked ELF.

The benchmark presets build the independent `benchmark.elf` and
`benchmark-packed.elf` executables. The collector launches `benchmark.elf`.
`STROOM_APP` selects `stroom` or `benchmark`; the presets set it automatically.

## Build the SDK

The development container's startup command builds the default SDK; later app
builds reuse it.

The builder fetches the SDK's pinned dependencies once, applies patches, then
freezes those sources for the isolated build. Nested SDK make targets otherwise
rerun the downloader and reset lwIP, removing its patches. Source hashes and
transmit regression checks run after the complete SDK build to catch that case.

```sh
make sdk
make sdk PRESET=stroom-diagnostics
```

Make initializes the required submodules. SDK builds use the pinned upstream
build processes with this repository's [patches](../../patches/README.md).
Only the ports needed by the player are built. For the environment and selection,
see the [Dockerfile](../../.devcontainer/Dockerfile) and [build script](build.sh) for more details.

Builds work on temporary copies of committed dependency sources, leaving the
submodules unchanged. Commit dependency edits before building them, and update
the parent repository's submodule pointer when adopting a new revision.

## Rebuilds and output

Completed installations are reused until explicitly rebuilt. After changing SDK
or ports revisions, patches applied by the [build script](build.sh), or SDK
diagnostic code, run the appropriate command:

```sh
make -B sdk
make -B sdk PRESET=stroom-diagnostics
```

For socket bridge changes, see its [app build path](../../patches/README.md#socket-bridge).

Make does not allow overriding the base build, SDK or installer directory.
`PRESET` selects the SDK variant for both app and launcher.

`make clean` clears `build/` except `build/sdk` and `build/installer`;
`make clean-all` removes both too.
Neither command removes custom output directories outside `build/`.

Make loads the generated SDK environment automatically. Before invoking CMake
directly, load the environment for the installation you intend to use, for example:

```sh
. build/sdk/release/environment.sh
```

## Build the launcher

Build a launcher using this project's patched SDK:

```sh
make ps2link
```

The release ELF is `build/stroom/<VIDEO_MODE>/ps2link/PS2LINK.ELF`. For diagnostic recording:

```sh
make ps2link PRESET=stroom-diagnostics
```

That ELF is `build/stroom-diagnostics/<VIDEO_MODE>/ps2link/PS2LINK.ELF`. Make builds a missing SDK first.
After changing SDK inputs, explicitly [rebuild the SDK](#rebuilds-and-output) with
the same preset before rebuilding the launcher.

For card deployment, follow the [PS2Link package and installation steps](../../setup-manual.md).

The build uses committed sources from the PS2Link submodule and leaves the
checkout untouched. `PS2LINK_SOURCE` can select another checkout; uncommitted
edits are not included.

Every build applies the [CD startup, display and direct-boot patches](../../patches/README.md#ps2link).

### Display configuration

After editing the [shared display profile](../../src/common/platform/graphics/display_config.h),
or its [refresh timing contract](../../shared/contracts/display.json), rebuild
the launcher and app. The launcher generates its own contract headers.
No SDK rebuild is needed for a profile change.
See [display integration](../../patches/README.md#display-integration), [display compatibility](../../troubleshooting.md#display-compatibility) and [diagnostics](../diagnostic/README.md) for more details.

## Installing rebuilt services

After rebuilding the SDK, rebuild the app with the same preset. Install it
according to how you boot:

- **Standalone:** replace the packed ELF at your console's boot path and fully
  restart the console. The app loads its embedded services at startup.
- **PS2Link:** when changes affect the launcher's resident network stack, also
  rebuild and install the launcher with the same preset. Updating the app alone
  does not replace that resident stack.

For a memory-card launcher, generate its replacement package:

```sh
make dev
```

Use the same `PRESET` and `VIDEO_MODE` as the app, then follow the
[card-copy procedure](../../setup-manual.md#2-copy-boot-to-memory-card-slot-1).
Fully restart the console and boot the replacement before launching the player.

To request a reset of the **currently installed** PS2Link:

```sh
make reset PS2_IP=YOUR_PS2_IP
```

Reloading the player or resetting PS2Link does not install a new launcher.

The app embeds audsrv and its private socket bridge. Changes limited to those
services require rebuilding the app and fully restarting the console, without
rebuilding PS2Link.
