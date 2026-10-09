# Send audio to the PS2

<!-- BEGIN TOC -->

## Contents

- [PCM input](#pcm-input)
- [Channel selection](#channel-selection)
- [Linux capture](#linux-capture)
- [Listening mode](#listening-mode)
- [macOS capture helper](#macos-capture-helper)
- [macOS login service](#macos-login-service)

<!-- END TOC -->

Replace `YOUR_PS2_IP` in the commands below with your console’s IP address.

Run the player with its network receiver available: leave the CD tray empty or
eject the selected disc. Use Node.js 18 or newer on the sending computer; no npm
packages are required.

## PCM input

From the repository root, feed raw **48 kHz signed 16-bit little-endian PCM** to
the sender through standard input:

```sh
node tools/aria/sender.mjs --host YOUR_PS2_IP < capture.pcm
```

## Channel selection

The sender defaults to stereo input. `--channels` sets the input channel count
(default: 2); `--left` and `--right` select the input channels sent to each output
(defaults: 1 and 2). Channel numbers start at 1.

For example, swap the left and right channels of stereo input:

```sh
node tools/aria/sender.mjs --host YOUR_PS2_IP --left 2 --right 1 < capture.pcm
```

## Linux capture

On Linux, you can pipe live capture from `arecord` into the sender. In this
example, `-c 10` tells `arecord` to capture ten channels. The sender reads that
input with `--channels 10` and selects channels 9 and 10 with `--left` and `--right`:

```sh
arecord -D plughw:CARD=USB,DEV=0 -t raw -f S16_LE -r 48000 -c 10 \
  | node tools/aria/sender.mjs --host YOUR_PS2_IP \
      --channels 10 --left 9 --right 10
```

Choose the device and channels for your own setup. To send desktop playback,
capture a loopback or monitor source. The sender does not convert sample formats
or strip WAV headers. It sends audio only, without metadata or artwork.

## Listening mode

Add `--listen` to animate MilkDrop without PS2 sound. `--device-name` adds an
optional display label and is valid only with listening:

```sh
node tools/aria/sender.mjs --listen --device-name "Desktop audio" \
  --host YOUR_PS2_IP < capture.pcm
```

The sender checks receiver support before sending audio. Stop and reconnect to
change listening mode. Ctrl+C stops the sender. End of input finishes the last
block with silence padding; malformed input or transport failure exits with an
error.

## macOS capture helper

The supplied helper is configured specifically for the Scarlett 18i20 capture
device and channels 9/10. Its device-name argument changes the label displayed
on the PS2, not the capture device or channel selection. For another setup,
adjust the capture command in [macos-listen.mjs](macos-listen.mjs).

With Node.js and FFmpeg installed, run directly in Terminal on the Mac:

```sh
node tools/aria/macos-listen.mjs "$(command -v ffmpeg)" \
  YOUR_PS2_IP "Focusrite 18i20"
```

Allow capture and network access when prompted, and verify that the PS2 meters
move. A successful network connection does not prove that capture contains audio.
The helper retries after a connection or capture process ends, allowing the PS2
to be switched on later.

## macOS login service

After verifying foreground capture, stop it and install the service as your
logged-in Mac user, without sudo:

```sh
node tools/aria/macos-service.mjs install YOUR_PS2_IP "Focusrite 18i20"
```

This installs and starts a user LaunchAgent. Check capture again in this launch
context. The service retries while the PS2 is unavailable; the Mac must remain
awake and the user session active.

```sh
node tools/aria/macos-service.mjs status
tail -F ~/Library/Logs/stroom/sender.log
node tools/aria/macos-service.mjs stop
node tools/aria/macos-service.mjs start
node tools/aria/macos-service.mjs uninstall
```

Logs rotate to keep storage bounded. Stop lasts until the next login or explicit
start; uninstall removes automatic startup but retains logs. Rerun install after
moving the repository, changing script locations or changing Node/FFmpeg paths,
because the LaunchAgent stores those paths.
