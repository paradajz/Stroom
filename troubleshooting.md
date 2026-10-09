# Troubleshooting

Start with the checks for your symptom below. Network and CD-recognition checks
work with release builds; diagnostic builds are needed only for console queries
and retained captures.

Run `make` commands from the repository root in the development container. Run
service, sender and packet-capture commands on the computer providing audio or
CD metadata. Replace `YOUR_PS2_IP` with the console's current displayed address.
The `tcpdump` examples use Linux's `any` interface; on macOS, use `tcpdump -D` to
find your LAN interface and substitute it for `any`.

| Symptom                                        | Start here                                                    |
| ---------------------------------------------- | ------------------------------------------------------------- |
| Stroom is missing or the sender cannot connect | [Discovery and connection](#discovery-and-connection)         |
| The app asks for a console restart             | [Console restart requests](#console-restart-requests)         |
| A CD plays without labels or covers            | [CD recognition](#cd-recognition)                             |
| Network labels or covers are missing           | [Network metadata and artwork](#network-metadata-and-artwork) |
| Audio cuts out or the visualizer freezes       | [Dropouts and freezes](#dropouts-and-freezes)                 |
| Listening connects but the meters do not move  | [Listening capture](#listening-capture)                       |
| The screen goes blank when the app starts      | [Display compatibility](#display-compatibility)               |

## Discovery and connection

Check that the console and sender are on the same network. Eject an inserted
audio CD before testing network playback: the selected source keeps ownership
until it becomes unavailable.

Try the console's displayed address and TCP port **12889** manually. Discovery
uses **UDP 12888** and requires a reachable broadcast network. If a manual
connection works but discovery does not, check broadcast reachability and
firewall rules for UDP 12888. Audio connections need TCP 12889.

To watch discovery and connection traffic on the sending computer:

```sh
sudo tcpdump -ni any 'udp port 12888 or tcp port 12889'
```

Packets visible in `tcpdump` can still be rejected by the firewall before an
application receives them.

Use the current console address for both the sender and `PS2_IP`. `PS2_IP`
selects the launch or query destination; it does not configure the console's
address. See [startup configuration](README.md#startup-configuration) for more details.

## Console restart requests

Use the reported message to choose the recovery below.

### Resident network module

**RESTART CONSOLE TO LOAD NETWORK MODULE** means a networking module already
loaded on the console is incompatible with this build.

Fully restart the console before trying again. When launching through PS2Link,
boot the [patched launcher](tools/sdk/README.md#build-the-launcher).
Reloading the app or using `make reset` does not replace incompatible resident
modules.

### CD drive

If the app or diagnostic log reports **CD DRIVE UNRESPONSIVE - RESTART REQUIRED**,
fully restart the console. A stalled read whose cancellation did not complete
leaves the drive unavailable. Reinserting the disc, retrying Play or reopening
audio sources does not restore it. Network playback remains available.

For ordinary CD playback errors, check that the disc is an audio CD and press
**Start** to retry preparation. Data and mixed-mode discs are unsupported.

### Sound output

**SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED** means shared sound output could
not start. Fully restart the console; changing sources or retrying CD preparation
does not restore it. The UI remains available while audio is disabled.

**CANNOT STOP AUDIO - RESTART CONSOLE** means source cleanup has not finished.
The player continues retrying and clears the message if cleanup succeeds. If it
persists, fully restart the console.

## CD recognition

CD playback works without the recognition service. Missing labels or covers do
not by themselves indicate a playback failure.

Check that the [CD service](tools/cd/README.md#run-the-service) is running and that
`lookup_host` in `STROOM.DAT` is the service computer's LAN IPv4 address. Restart
the player after changing this setting. If the service was started with `--ps2`,
that restriction must match the console's current address.

### The service prints no requests

On the service computer, check whether lookup packets arrive:

```sh
sudo tcpdump -ni any 'udp port 12890'
```

If no packets arrive, check `lookup_host`, the console's network connection and
the route to the service computer. If packets arrive but the service prints no
requests, check its address restriction and the host firewall. Allow inbound
**UDP 12890** for lookup messages and **TCP 12890** for cover downloads.

If firewall rules restrict requests by source IP, allow the console's current
address or your LAN subnet (for example, `192.168.1.0/24`). A rule tied to an old
console address will block recognition after DHCP assigns a different one.
With firewalld, apply the rule to the network interface's active zone and save
both runtime and permanent configuration. See
[firewalld rules](https://firewalld.org/documentation/man-pages/firewall-cmd.html).
Packets visible in `tcpdump` can still be blocked before reaching the service.

### Requests arrive, but labels or covers are missing

Read the service terminal output and its saved JSON report. A lookup error is
different from finding no acceptable match. Labels are published before artwork
is ready, so a successful recognition can still have a pending or failed cover.
See [results and matching](tools/cd/README.md#results-and-matching)
and [cache and retries](tools/cd/README.md#cache-and-retries) for more details.

If labels arrive but a cover does not, also check TCP 12890 through the host
firewall. To watch both lookup and cover traffic:

```sh
sudo tcpdump -ni any 'udp port 12890 or tcp port 12890'
```

`make diagnostics` queries the AriaCast receiver. A selected CD closes that
receiver, so use service logs, reports and packet captures for CD recognition.

## Network metadata and artwork

The [PC sender](tools/aria/README.md) sends audio only, without labels or covers.
For senders that provide metadata, check the
[artwork transport rules](src/common/audio/artwork/README.md#transport-and-failure-handling):
cover URLs must use plain HTTP with a numeric IPv4 host, and image data must be
JPEG or PNG. See the [display rules](src/common/ui/README.md#metadata-and-artwork) for more details.

If the problem persists, [launch a diagnostic build](tools/diagnostic/README.md)
and query immediately after the problem, before changing tracks:

```sh
make diagnostics PS2_IP=YOUR_PS2_IP DIAGNOSTIC=metadata
make diagnostics PS2_IP=YOUR_PS2_IP DIAGNOSTIC=artwork
```

The first shows the last metadata request and whether it was accepted. The second
shows download errors and cover preparation timings. If queries receive no
reply, check the console address, confirm a diagnostic build is running and keep
the network receiver active with the CD ejected.

## Dropouts and freezes

Follow the [recording guide](tools/diagnostic/README.md#record-a-dropout) to prepare the
diagnostic launcher and app, collect a dropout and free the saved capture slots.
For network cover freezes, query artwork diagnostics immediately afterward.

An empty output queue or a long read gap alone does not identify the cause of an
audible dropout. Compare activity around the event before changing buffering.
The [capture reference](tools/diagnostic/README.md#reading-diagnostic-captures) explains fields,
timing alignment and coverage limits.

## Listening capture

A successful connection does not prove that the computer is capturing audio.
Check that the PS2 meters move, then verify the capture device and selected input
channels in the [sender guide](tools/aria/README.md). Listening mode intentionally
produces no PS2 sound.

For the macOS login service, inspect its status and log on the Mac:

```sh
node tools/aria/macos-service.mjs status
tail -F ~/Library/Logs/stroom/sender.log
```

Verify foreground capture before installing the service, then check capture
again in the login-service context. The Mac must remain awake with the user
session active; see [macOS login service](tools/aria/README.md#macos-login-service) for more details.

## Display compatibility

The app currently outputs **480p progressive video at approximately 59.94 Hz**,
with a 640×480 framebuffer. Your cable and display or scaler must support this
signal. A visible PS2 boot screen or another launcher does not establish compatibility:
they can use a different video mode. There is no automatic interlaced fallback.

### Connecting the console

- **Component (YPbPr):** use a component cable, select YPbPr in the PS2 system
  configuration, and use a display or scaler input that accepts 480p component.
- **RGB:** the PS2 uses sync-on-green (**RGsB**) for 480p. The cable and receiver
  must support it. An input expecting separate sync (**RGBS**) can lose sync when
  the app starts, even though the launcher was visible.
- **Composite, S-video, or a standard-definition-only TV:** these do not support
  the app's current output mode. Use a compatible connection and display.

On an OSSC with RGB connected to AV1, select RGsB for this output. The launcher
built by this repository uses the same profile, so it needs no input change
when launching the app. The `525-p` indication includes blanking lines; the
visible picture is 480 lines. See the [OSSC input documentation](https://junkerhq.net/xrgb/index.php/OSSC)
for receiver settings. Returning to an interlaced launcher can require switching
back to RGBS.

Progressive output avoids the alternating fields and deinterlacing that can make
stationary text and thin borders tremble. It does not guarantee that every preset
renders at the display's refresh rate.
