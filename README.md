<picture>
  <source media="(prefers-color-scheme: dark)" srcset="bin/logo.svg">
  <source media="(prefers-color-scheme: light)" srcset="bin/logo-light.svg">
  <img alt="Stroom logo" src="bin/logo-light.svg">
</picture>

A PlayStation 2 CD player and AriaCast network streamer with a MilkDrop visualiser. CD playback includes anti-skip protection and optional CD metadata recognition with artwork download.

**Network streaming:**

![Streaming demo](bin/stream.gif)

**CD playback:**

![CD playback demo](bin/cd.gif)

**Silent listening mode:**

![Listening mode demo](bin/listen.gif)

<!-- BEGIN TOC -->

## Contents

- [Start Stroom from a memory card](#start-stroom-from-a-memory-card)
- [Build and run](#build-and-run)
- [Launch through PS2Link](#launch-through-ps2link)
- [Audio and CD recognition](#audio-and-cd-recognition)
  - [Listen to a PS2 CD on your computer](#listen-to-a-ps2-cd-on-your-computer)
  - [Recognize CDs and fetch cover art](#recognize-cds-and-fetch-cover-art)
  - [Send computer audio to the PS2](#send-computer-audio-to-the-ps2)
- [Startup configuration](#startup-configuration)
- [Development](#development)
- [License](#license)

<!-- END TOC -->

## Start Stroom from a memory card

Download the package for your display from a successful
[CI run](https://github.com/paradajz/Stroom/actions/workflows/ci.yml), under
**Artifacts**:

| Video mode | Stroom            | PS2Link          |
| ---------- | ----------------- | ---------------- |
| `480i`     | **prod-480i.zip** | **dev-480i.zip** |
| `480p`     | **prod-480p.zip** | **dev-480p.zip** |

Each archive contains the ready-to-copy `BOOT/` directory. For the one-time
signed-loader installation on an unmodded console, also download
**installer.zip**, which is independent of video mode. Follow the
[installation guide](setup-manual.md) to install the packages.

## Build and run

Run build commands from the repository root **inside the development container**.
The default video mode is `480i` for NTSC-compatible displays. Check [display compatibility](troubleshooting.md#display-compatibility)
and [video build options](tools/sdk/README.md#video-mode) before selecting `480p`.

To build the packages locally:

```sh
make prod
```

For this repository's patched PS2Link on the card instead:

```sh
make dev
```

For the one-time signed-loader installation on an unmodded console with a
MagicGate card:

```sh
make prod installer
```

Follow the [memory-card installation guide](setup-manual.md) to copy the package
and configure Matrix (on modded console) DEV1 or install the signed loader (unmodded console).

## Launch through PS2Link

Boot this repository's patched PS2Link built with `make dev`
then replace `YOUR_PS2_IP` with the console's displayed IP address:

```sh
make run PS2_IP=YOUR_PS2_IP
```

This builds and launches the app. See [build options and output directories](tools/sdk/README.md).

## Audio and CD recognition

Run the following commands from the repository root **on the computer providing
or receiving audio and metadata**, rather than in a development container.
An inserted audio CD plays automatically by default. With no CD inserted, Stroom
accepts network audio. See the [PS2 controls](src/common/ui/README.md).

### Listen to a PS2 CD on your computer

Requires Linux, a C++17 compiler, CMake, Make, PipeWire and RealtimeKit:

```sh
make cd-listen
```

See the [listener guide](tools/cd/listener/README.md) for configuration,
background service installation and diagnostics.

### Recognize CDs and fetch cover art

Requires Make, Node.js 24 LTS (latest 24.x patch) and FFmpeg:

```sh
make cd-recognize
```

On the first run, set `contact` in the generated `~/.config/stroom/config.json`,
then run again. Follow the [recognition guide](tools/cd/recognition/README.md)
to connect the PS2, configure hosting or install a background service.

### Send computer audio to the PS2

Select **Stroom** in AriaCast, or send a raw 48 kHz signed 16-bit little-endian
stereo PCM recording with Node.js 18+:

```sh
node tools/aria/sender.mjs --host YOUR_PS2_IP < capture.pcm
```

For live Linux/macOS capture and silent visualizer input, follow the
[audio sender guide](tools/aria/README.md).

## Startup configuration

Copy [STROOM.DAT.example](STROOM.DAT.example) to `STROOM.DAT` in the repository
root and edit it for your network and startup preferences:

```sh
cp STROOM.DAT.example STROOM.DAT
```

The example documents each field and its accepted values.

`make run` copies the local file beside the ELF on every launch; `make prod`
includes it in the memory-card package, using the example when no local file
exists. `make dev` derives PS2Link's static network settings from the same file;
see [package settings](setup-manual.md#package-settings). For other launchers, place it beside
the ELF yourself. The app reads it once at startup;
relaunch the app after editing it. A missing file uses defaults. Invalid values
and unknown keys are ignored. Menu changes are not saved across app restarts.

Existing launcher addresses or DHCP configuration are preserved. An unconfigured
interface uses static addressing only when valid `ip`, `netmask` and `gateway`
values are all supplied; otherwise it requests DHCP.

## Development

| Command          | Purpose                                            |
| ---------------- | -------------------------------------------------- |
| `make`           | Build the app and any missing SDK                  |
| `make test`      | Run host tests                                     |
| `make lint`      | Check C code                                       |
| `make format`    | Format project files in place                      |
| `make clean`     | Clean `build/` while preserving SDKs and installer |
| `make clean-all` | Also remove SDK installations and installer        |

See [build options](tools/sdk/README.md#building-the-application), [architecture](src/README.md),
[host tools](tools/README.md), and [tests](tests/README.md) for more details.

## License

Project code is licensed under [BSD 3-Clause](license.txt). Dependencies retain
their own licenses; see [third-party notices](third_party/README.md).
