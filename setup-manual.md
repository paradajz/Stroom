# Install Stroom or PS2Link from a memory card

Build one application package, then choose the boot method for your console:

| Console and card                                                        | Installation steps                                                                                                                                                        |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Matrix-compatible modchip; genuine or unofficial card                   | [Build](#1-build-the-application-package), [copy BOOT](#2-copy-boot-to-memory-card-slot-1), then [configure DEV1](#3-configure-matrix-dev1)                               |
| Unmodded console supporting memory-card updates; genuine MagicGate card | [Build](#1-build-the-application-package), [copy BOOT](#2-copy-boot-to-memory-card-slot-1), then [install signed PS2BBL](#4-unmodded-consoles-install-signed-ps2bbl-once) |

## 0. Start a file manager with FreeDVDBoot

Skip this step if you can already run uLaunchELF or wLaunchELF.

For a **Slim**, use the [official FreeDVDBoot instructions](https://github.com/CTurt/FreeDVDBoot#easy-setup-for-all-ps2-slim-consoles--bravia-tv):

1. Download [All PS2 Slims - English language.iso](https://github.com/CTurt/FreeDVDBoot/raw/master/PREBUILT%20ISOs/All%20PS2%20Slims%20-%20English%20language.iso).
2. **Burn the ISO as a disc image to a DVD-R**, using the lowest supported speed
   and finalizing the disc. Copying the ISO file onto a data disc will not work;
   use a DVD, not a CD.
3. Boot without a disc and set **System Configuration → Language → English**.
4. On a Matrix-modded console, select **BOOT MODE → AUTO** using the
   [Matrix menu](#3-configure-matrix-dev1), so DEV1 does not launch the card instead.
5. Insert the DVD and boot it. FreeDVDBoot opens uLaunchELF. If the modchip makes
   it appear as a PS2 game instead of DVD Video, temporarily disable the chip
   following its instructions.
6. Select **FileBrowser**. Your USB stick appears as **mass:/**; use it for the
   package-copy steps below.

For a **Fat**, check [DVD-player compatibility](https://github.com/CTurt/FreeDVDBoot#phat-consoles)
first; the Slim ISO is not suitable. Upstream lists SCPH-39004 with DVD Player 2.10
as not working. You can instead prepare its memory card on the Slim using
[the signed-loader installation](#4-unmodded-consoles-install-signed-ps2bbl-once).

FreeDVDBoot provides the file manager for installation. Once memory-card boot
is configured, remove the DVD; it is not needed for normal startup.

## 1. Build the application package

Run from the repository root **inside the development container**.

**Stroom:**

```sh
make prod
```

**PS2Link:**

```sh
make dev
```

| Application | Installation folder                  |
| ----------- | ------------------------------------ |
| Stroom      | `build/stroom/480i/app/memcard/`     |
| PS2Link     | `build/stroom/480i/ps2link/memcard/` |

Each package contains exactly two files. Production contains:

```text
BOOT/BOOT.ELF       # packed Stroom application
BOOT/STROOM.DAT     # editable application and network settings
```

Development contains:

```text
BOOT/BOOT.ELF       # packed PS2Link, with its modules embedded
BOOT/IPCONFIG.DAT   # editable IP address, netmask and gateway
```

Both packages put the selected application itself at `BOOT/BOOT.ELF`; the
one-time signed loader for unmodded consoles is installed separately.
Rebuilding replaces only the selected generated package. See
[video modes](tools/sdk/README.md#video-mode) and
[preset-specific output directories](tools/sdk/README.md#output-directories)
for progressive or diagnostic builds. The default is `480i`.

### Package settings

Configure the repository-root [STROOM.DAT](README.md#startup-configuration) before
building, or edit the generated settings before copying them to the card.
Production packages use that file (or the example when absent) as
`BOOT/STROOM.DAT`.

`make dev` builds this repository's patched PS2Link. When local `STROOM.DAT`
supplies `ip`, `netmask` and `gateway`, the package uses those values for
`BOOT/IPCONFIG.DAT`. All three must be valid IPv4 addresses. With no static
network settings, it copies `third_party/ps2link/bin/IPCONFIG.DAT`, whose defaults
are `192.168.0.10 255.0.0.0 192.168.0.1`.

**Check `BOOT/IPCONFIG.DAT` before copying.** Its first line is:

```text
IP NETMASK GATEWAY
```

PS2Link uses static addressing. Keep `IPCONFIG.DAT` beside its `BOOT.ELF`.

After PS2Link starts, use the [launch command](README.md#launch-through-ps2link)
with the same preset and video mode as the launcher.

## 2. Copy BOOT to memory-card slot 1

Open the file manager using [FreeDVDBoot](#0-start-a-file-manager-with-freedvdboot)
or another working entry point. The example below installs the default production package:

1. Copy `build/stroom/480i/app/memcard/` onto USB as `memcard/`.
2. Open wLaunchELF's file browser and enter `mass:/memcard/`.
3. Highlight **BOOT**, then press **R1 → Copy**.
4. Navigate to **`mc0:/`**, then press **R1 → Paste**.
5. Merge into the existing BOOT directory and overwrite matching files. Wait until
   copying finishes before powering off.

The installed production paths must be:

```text
mc0:/BOOT/BOOT.ELF
mc0:/BOOT/STROOM.DAT
```

For development, copy the `memcard/` from the PS2Link build instead; its settings
file is `mc0:/BOOT/IPCONFIG.DAT`.

Copy the folder **inside** the generated package. Do not create
`mc0:/memcard/BOOT/`. To switch between production and development, copy the new
package's BOOT folder and overwrite the executable and settings. Existing files
outside the package are not removed.

Button mappings vary between uLaunchELF and wLaunchELF builds. Follow the
on-screen legend for opening folders, launching ELFs and moving up a level;
**R1** opens file operations.

## 3. Configure Matrix DEV1

1. Turn the console off to the red standby light.
2. Hold **Triangle + Circle** on controller 1 and turn the console on.
3. Release the buttons when the Matrix configuration menu appears.
4. Highlight **BOOT MODE** and press **X** until it reads **DEV1**.
5. Press **START** to save, wait for confirmation, then power off.
6. Remove the DVD and USB stick. Leave the prepared card in slot 1.
7. Power on normally.

The boot sequence is:

```text
Matrix DEV1 -> mc0:/BOOT/BOOT.ELF -> Stroom or PS2Link
```

There is no bootloader button window or bundled recovery shortcut. To use the
FreeDVDBoot file manager again, change Matrix BOOT MODE back to AUTO and boot
the disc.

## 4. Unmodded consoles: install signed PS2BBL once

This route uses the console's memory-card system-update mechanism to start
**PS2BBL**, which automatically launches the same `mc0:/BOOT/BOOT.ELF` used above.
It requires a card that passes MagicGate authentication and a console that accepts
memory-card system updates. This has been tested on **SCPH-39004** and **SCPH-75004**
consoles. Late Slims with ROM 2.30 do not support this automatic route.

The intended sequence is:

```text
Power on -> signed PS2BBL -> mc0:/BOOT/BOOT.ELF -> Stroom or PS2Link
```

OpenTuna requires entering and leaving the memory-card browser to trigger its
exploit. This signed route starts automatically and does not use an FMCB runtime.

### Build and install

You can download **installer.zip** and the Stroom or PS2Link archive for your
video mode from the [CI artifacts](README.md#start-stroom-from-a-memory-card). Extract
installer.zip into a folder named `installer/` and the application archive into
`memcard/`, then follow the copy/install steps below. To build them locally:

**Stroom and its one-time installer:**

```sh
make prod installer
```

**PS2Link and its one-time installer:**

```sh
make dev installer
```

The installer is generated in `build/installer/`, independently of `PRESET` and
`VIDEO_MODE`. Its upstream executable handles video detection.

1. Copy the entire `installer/` directory and the selected `memcard/` directory
   onto a USB stick. Keep the installer's subdirectories together.
2. Start the file manager using [FreeDVDBoot](#0-start-a-file-manager-with-freedvdboot)
   or another working entry point. Copy the app's **BOOT** directory into `mc0:/`
   as in section 2. Use a genuine MagicGate card in slot 1.
   The Slim can prepare the card for the Fat; the Fat does not need to run an
   installer itself.
3. In wLaunchELF, copy the **entire** `mass:/installer/` directory to
   `mc0:/installer/`, including all its subdirectories. Launch
   **`mc0:/installer/KELFBinder.elf`** from the card with its subdirectories in
   place.
4. Select **Manage System Updates -> MagicGate Test**, then select the target
   card. Continue only after **Test succeeded!** This test does not write to the
   card. A `-22` signing failure rules that card out for this route.
5. Return to the main menu and ensure **Extra files will be installed** is shown
   (**R1** enables it). This copies our small loader configuration, not bundled
   applications.
6. Select **Manage System Updates -> Advanced Install -> Cross Model**, then
   select the target card and finish installation. Cross Model covers multiple
   console models. Normal Install can select a model-specific filename.
7. Power off. Move the card to the unmodded console's slot 1, remove USB and any
   disc, then power on. It should start the app automatically.

Use a spare formatted card, or back up an existing card before replacing its
boot setup. If KELFBinder finds old system-update directories, its clean-folder
prompt replaces those boot files. If it reports an FMCB **Multi Installation**,
use that installation's original uninstaller first; do not manually delete its
cross-linked files.

To check the signed path on the modded Slim, set Matrix **BOOT MODE to AUTO** so
DEV1 cannot bypass the signed loader. AUTO still leaves the modchip active; only
a test with the chip disabled or on the unmodded Fat confirms unmodded operation.

### What the installer changes

`make installer` downloads **KELFBinder v1.1.1** and the standard memory-card
**PS2BBL KELF build 1a2af22 (2025-06-28)**, verifies pinned SHA-256 checksums, and
generates a separate installer package. Downloads are reused under
`build/downloads/signed-boot/`.

KELFBinder signs PS2BBL **for the physical target card on the PS2**. Copying an
unsigned ELF or copying a signed update from another card does not replace this
step. The installer adds the regional system-update files and
`mc0:/SYS-CONF/PS2BBL.INI`. It does not copy or overwrite `BOOT/BOOT.ELF` and does
not install OPL, wLaunchELF, OpenTuna or a decrypted FMCB menu.

The [loader configuration](tools/setup/PS2BBL.INI) selects `mc0:/BOOT/BOOT.ELF`
and removes the configured key-selection wait, banner and history read. Upstream
PS2BBL still has a **fixed two-second emergency-key window** before it reads this
configuration, plus service initialization and the console's own startup time.
Changing `KEY_READ_WAIT_TIME` does not remove that two-second window.
Actual boot time has not been measured, so this is not a claim that PS2BBL is
faster than every FMCB version.

For later updates or switching between Stroom and PS2Link, rebuild `make prod` or
`make dev` and replace the BOOT files. **Do not reinstall the signed loader.**
During the built-in two-second window, **R1 + START** enters recovery and tries to
load `mass:/RESCUE.ELF`. A recovery ELF is not bundled; supply a compatible file
manager yourself. Removing the card lets the console boot normally.

Upstream references: [KELFBinder installation modes and MagicGate test](https://israpps.github.io/KELFBinder/documentation/features.html),
[system-update compatibility and paths](https://israpps.github.io/KELFBinder/documentation/sysupdate-docs.html),
[PS2BBL configuration](https://israpps.github.io/PlayStation2-Basic-BootLoader/documentation/configuration.html),
[the pinned loader's two-second emergency window](https://github.com/israpps/PlayStation2-Basic-BootLoader/blob/1a2af22b3e51621b197489cb0f53e15aee71637d/src/main.c#L179).
