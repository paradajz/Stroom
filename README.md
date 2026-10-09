<picture>
  <source media="(prefers-color-scheme: dark)" srcset="bin/logo.svg">
  <source media="(prefers-color-scheme: light)" srcset="bin/logo-light.svg">
  <img alt="Stroom logo" src="bin/logo-light.svg">
</picture>

A PlayStation 2 CD player and AriaCast network streamer with a MilkDrop visualiser.

![Streaming demo](bin/stream.gif)

![CD playback demo](bin/cd.gif)

![Listening mode demo](bin/listen.gif)

<!-- BEGIN TOC -->

## Contents

- [Build and run](#build-and-run)
- [Startup configuration](#startup-configuration)
- [Development](#development)
- [License](#license)

<!-- END TOC -->

## Build and run

Open the repository in its development container. Before launching, check [display compatibility](troubleshooting.md#display-compatibility).

From the repository root in the container, replacing `YOUR_PS2_IP` with your console’s IP address:

```sh
make run PS2_IP=YOUR_PS2_IP
```

This builds `build/stroom/stroom.elf` and launches it
through PS2Link. The [replacement launcher](tools/sdk/README.md#build-the-launcher) is required.

By default, an inserted audio CD starts playing automatically. Without a CD, the app waits
for network audio and continues checking for inserted discs. An active network
stream keeps control until it ends. Select **Stroom** in AriaCast, or use the
[PC sender](tools/aria/README.md). Listening mode drives the visualizer without
sound output from the PS2. See the [controls](src/common/ui/README.md).

[CD recognition](tools/cd/README.md) is optional. Its PC service supplies track
labels and artwork; CD playback does not require the service.

## Startup configuration

Copy [STROOM.DAT.example](STROOM.DAT.example) to `STROOM.DAT` in the repository
root and edit it for your network and startup preferences:

```sh
cp STROOM.DAT.example STROOM.DAT
```

The example documents each field and its accepted values. The local file is
ignored by Git.

`make run` copies the local file beside the ELF on every launch. For other
launchers, place it beside the ELF yourself. The app reads it once at startup;
relaunch the app after editing it. A missing file uses defaults. Invalid values
and unknown keys are ignored. Menu changes are not saved across app restarts.

Existing launcher addresses or DHCP configuration are preserved. An unconfigured
interface uses static addressing only when valid `ip`, `netmask` and `gateway`
values are all supplied; otherwise it requests DHCP.

## Development

| Command          | Purpose                                           |
| ---------------- | ------------------------------------------------- |
| `make`           | Build the app and any missing SDK                 |
| `make test`      | Run host tests                                    |
| `make lint`      | Check C code                                      |
| `make format`    | Format project files in place                     |
| `make clean`     | Clean `build/` while preserving SDK installations |
| `make clean-all` | Also remove SDK installations                     |

See [build options](tools/sdk/README.md#building-the-application), [architecture](src/README.md),
[host tools](tools/README.md), and [tests](tests/README.md) for more details.

## License

Project code is licensed under [BSD 3-Clause](license.txt). Dependencies retain
their own licenses; see [third-party notices](third_party/README.md).
