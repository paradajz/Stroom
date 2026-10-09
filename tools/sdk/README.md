# Build tools

<!-- BEGIN TOC -->

## Contents

- [Building the application](#building-the-application)
- [Build the SDK](#build-the-sdk)
- [Rebuilds and output](#rebuilds-and-output)
- [Build the launcher](#build-the-launcher)
  - [Display configuration](#display-configuration)
- [Installing rebuilt services](#installing-rebuilt-services)

<!-- END TOC -->

Run Make commands from the repository root in the development container.

## Building the application

`PRESET` selects the CMake configuration and matching SDK. The default is `stroom`;
`stroom-diagnostics` enables inspection and recording. See
[CMakePresets.json](../../src/CMakePresets.json) for configuration values.

App outputs normally go under `build/<PRESET>`; `BUILD_DIR_APP` can select a
different app build directory. Use `make configure-app` to prepare the selected
configuration and SDK without building the app.

App builds produce both `stroom.elf` for PS2Link and a compressed `stroom-packed.elf`
for memory-card boot in that build directory. Packing runs again when the app
changes or `stroom-packed.elf` is missing. Copy `stroom-packed.elf` to your console's memory-card
boot path for standalone launch.

The benchmark presets build the independent `benchmark.elf` and
`benchmark-packed.elf` executables. The collector launches `benchmark.elf`.
`STROOM_APP` selects `stroom` or `benchmark`; the presets set it automatically.

## Build the SDK

The development container's startup command builds the default SDK; later app
builds reuse it.

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

Release and diagnostic installations are separate under the fixed `build/sdk`
root. Make does not allow overriding the base build or SDK directory.
`PRESET` selects the variant for both app and launcher.

`make clean` clears `build/` except `build/sdk`; `make clean-all` removes it too.
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

The release ELF is `build/ps2link/release/PS2LINK.ELF`. For diagnostic recording:

```sh
make ps2link PRESET=stroom-diagnostics
```

That ELF is `build/ps2link/diagnostic/PS2LINK.ELF`. Make builds a missing SDK first.
After changing SDK inputs, explicitly [rebuild the SDK](#rebuilds-and-output) with
the same preset before rebuilding the launcher.

The build uses committed sources from the PS2Link submodule and leaves the
checkout untouched. `PS2LINK_SOURCE` can select another checkout; uncommitted
edits are not included.

Every build applies the [CD startup and display patches](../../patches/README.md#ps2link).

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

To install a launcher, replace its `PS2LINK.ELF`, preserving configuration and
icon files. Fully restart the console and boot the replacement before launching
the player. `make reset PS2_IP=YOUR_PS2_IP` requests a PS2Link reset; replace the address
with your console's IP. Reloading the player or resetting PS2Link does not install
a new launcher.

The app embeds audsrv and its private socket bridge. Changes limited to those
services require rebuilding the app and fully restarting the console, without
rebuilding PS2Link.
