# CD listener

<!-- BEGIN TOC -->

## Contents

- [Stream CD audio to this computer](#stream-cd-audio-to-this-computer)
- [Listener configuration](#listener-configuration)
- [Command-line flags](#command-line-flags)
- [Install as a background service (Linux)](#install-as-a-background-service-linux)
- [Playback and discovery](#playback-and-discovery)
  - [Continuous playback and reconnection](#continuous-playback-and-reconnection)
  - [PC buffering](#pc-buffering)
- [Discovery and firewall](#discovery-and-firewall)
  - [firewalld exception](#firewalld-exception)
- [Diagnose CD dropouts](#diagnose-cd-dropouts)
  - [Timing and counters](#timing-and-counters)
  - [Capture an existing listener](#capture-an-existing-listener)
  - [Start a listener and capture together](#start-a-listener-and-capture-together)
  - [Read a saved capture](#read-a-saved-capture)
  - [SDK and launcher updates](#sdk-and-launcher-updates)

<!-- END TOC -->

Run these tools on the computer receiving CD audio. Commands below run from the repository root.

## Stream CD audio to this computer

Install a C++17 compiler, Make, CMake
3.21 or newer, pkg-config, and the PipeWire and D-Bus development packages.
Playback needs a running PipeWire desktop session, an available output, and
RealtimeKit.

For example, on openSUSE:

```sh
sudo zypper install gcc-c++ make cmake pkgconf-pkg-config pipewire-devel dbus-1-devel rtkit
```

On Debian or Ubuntu:

```sh
sudo apt install g++ make cmake pkg-config libpipewire-0.3-dev libdbus-1-dev rtkit
```

Build and start it on the listening computer:

```sh
make cd-listen
```

The executable is `build/cd-listener/stroom-cd-listener`. The optional diagnostic
scripts in this directory require Node.js 18 or newer and their listed capture
tools.

Keep CD selected on the PS2. You can start the listener before or after CD
playback; it discovers the console and waits for audio. No PC destination
address or output selection is configured on the PS2. Local PS2 sound stays
enabled.

## Listener configuration

The first run creates `~/.config/stroom/listener.json`:

```json
{
  "host": null,
  "buffer_ms": 500
}
```

- `host`: `null` discovers the PS2 automatically, or set a numeric IPv4 address
  such as `"192.168.1.240"`.
- `buffer_ms`: startup and recovery reserve in milliseconds. `0` disables the reserve. The maximum is `2000`.

Foreground listening and the installed service read this file at startup.
After editing it, restart the listener; see the
[service controls](#install-as-a-background-service-linux) for background runs.

To select another existing config file for foreground listening or service
installation:

```sh
make cd-listen CONFIG=/path/to/listener.json
make install-cd-listener CONFIG=/path/to/listener.json
```

Explicitly selected files must exist. Missing settings use the defaults shown
above; unknown settings, duplicate keys and invalid values stop startup with an
error naming the file.

## Command-line flags

The listener accepts these flags:

| Flag             | What it does                                                                    |
| ---------------- | ------------------------------------------------------------------------------- |
| `--config FILE`  | Reads settings from the specified JSON file instead of the default file.        |
| `--check-config` | Validates the config, prints its absolute path and exits without playing audio. |
| `--list`         | Lists the PS2 consoles it finds, then exits without playing audio.              |
| `--help`, `-h`   | Shows the available flags and exits.                                            |

Pass flags through `ARGS` when launching with Make. For example, to list consoles:

```sh
make cd-listen ARGS="--list"
```

Or pass them directly to the executable:

```sh
build/cd-listener/stroom-cd-listener --config /path/to/listener.json --check-config
```

## Install as a background service (Linux)

Run this on the listening computer as your normal desktop user, without `sudo`.
Stop any foreground listener first so it does not play a second copy of the audio.
The build dependencies above, Node.js 18 or newer, systemd and `flock`
(util-linux) must be installed. Node.js runs the installer; the service executes
the native listener directly.

```sh
make install-cd-listener
```

Installation copies the executable into
`~/.local/share/stroom/cd-listener/` and enables
`~/.config/systemd/user/stroom-cd-listener.service`. `XDG_DATA_HOME` and
`XDG_CONFIG_HOME` override those roots when set to absolute paths. The service
runs independently of the checkout. Run the install command again to update the
executable. The unit saves the configuration's absolute path and reads that file
on every start. Changing the selected file requires reinstalling. A failed update
restores the previous files and service state. The installer accepts `--config FILE`; the other
[command-line flags](#command-line-flags) are for foreground use.

```sh
systemctl --user status stroom-cd-listener.service
journalctl --user -u stroom-cd-listener.service -f
systemctl --user restart stroom-cd-listener.service
systemctl --user stop stroom-cd-listener.service
systemctl --user start stroom-cd-listener.service
```

The enabled service starts with your systemd user manager, normally at login,
and stays running while that manager runs. A PipeWire failure, unavailable output or denied audio-thread
priority makes the process exit; systemd retries after ten seconds. An explicit
`systemctl --user stop` leaves it stopped until you start it or the user manager
starts again. A running process can still be waiting for a console or output;
check the journal for playback and scheduling messages.

Login lingering can keep the user manager running without a login, but does not itself
provide an audio device, desktop session or permission for real-time scheduling.
The installer does not enable lingering or change those permissions. Test playback
and recovery on the listening computer after login and after an audio-service
restart.

The unit limits journal messages to 100 per 30 seconds; journal retention follows
the host's journald configuration. See [timing and counters](#timing-and-counters)
for the logged statistics.

To stop, disable and remove the installed listener:

```sh
make uninstall-cd-listener
```

Uninstallation retains the listener configuration, source checkout and host
audio/firewall settings.

## Playback and discovery

Set `host` in the configuration file if discovery fails or several consoles are
found. Automatic discovery queries each local IPv4 subnet. A bridged development
container generally cannot discover devices on the host LAN through broadcast,
although a direct connection using `host` may work.

Discovery uses **UDP 12891**; audio uses **TCP 12891**, with up to four listeners.
Permit these connections between the devices. This outgoing CD transport is
separate from incoming AriaCast audio.

Audio is **48 kHz signed 16-bit little-endian stereo PCM**, copied from successful
CD resampler output. Track controls stay on the PS2. Sound/L2 affect local PS2
volume; control PC volume on the computer. The stream carries audio only.
PS2 and PC playback have independent buffering and are not synchronized.

### Continuous playback and reconnection

The listener reconnects after manual track changes, pause, seeking, disc changes
or a lost connection, discarding buffered audio from the interrupted session.
Automatic Repeat, Shuffle and Program transitions retain the connection and
queued audio. Normal playback completion drains the PC buffer, even for a clip
shorter than the configured reserve, with a five-second deadline.

TCP keepalive detects connections lost without a clean disconnect. If a
discovered console becomes unavailable, the listener discovers again, including
after its address changes. A healthy connection can wait while playback is
paused or stopped, without an audio-inactivity deadline.

Slow connections are disconnected and retried without stalling local CD playback.
After an interruption, the listener queries the console for its last disconnect
reason and sender counters. See [server queue and timeout policy](../../../src/common/audio/README.md#outgoing-cd-audio)
for the opening window, session boundaries and transport limits.

### PC buffering

The listener queues received PCM in a fixed ring before starting playback,
giving the PC a reserve for short delivery or scheduling gaps. Its duration is
the [configured `buffer_ms`](#listener-configuration); a larger reserve adds
startup and recovery delay. The ring has fixed capacity for about 2.7 seconds
of audio and applies backpressure when full. A stalled output fails after five seconds.

If playback exhausts the reserve, the listener counts an underrun, logs
`Rebuffering ...`, and collects the configured amount again. It preserves sample
order and keeps the same connection. The audio callback supplies silence during
the gap.

The PipeWire audio callback only copies queued samples and fills shortages with
silence. The ordinary receiver thread requests `SCHED_RR` priority 5 for that
specific callback thread through RealtimeKit and verifies the kernel policy,
priority and reset-on-fork flag before enabling audio. It checks the grant once
per second during playback. A denial, revocation, missing output or PipeWire
failure stops the listener with an explicit error. Discovery, socket reception,
logging and diagnostic queries retain ordinary scheduling priority. No system
scheduler settings or user-wide priority limits are changed.

The PC reserve covers network delivery gaps. The PS2's separate disc reserve
covers interrupted CD reads; see [disc and PC buffers](../../../troubleshooting.md#disc-and-pc-buffers).

## Discovery and firewall

If streaming with a configured `host` works but automatic discovery fails on Linux,
leave the listener running and use a second terminal on the same PC:

```sh
node tools/cd/listener/diagnose-discovery.mjs YOUR_PS2_IP
```

The script finds the LAN interface, requests sudo for a seven-second UDP packet
capture and read-only firewall inspection, and compares broadcast with direct
discovery. It requires `ip`, `tcpdump` and `timeout`; it changes no network or
firewall settings. Use `--firewall-only` to inspect the firewall without repeating
the capture. Share the complete output to locate where discovery fails.

The listener uses a temporary UDP reply port chosen by the operating system.
Broadcast queries receive unicast replies from the PS2's UDP source port 12891,
which a PC firewall may not associate with the broadcast query. If the capture
sees replies but the listener does not, check rules for those incoming replies.
Opening UDP destination port 12891 on the PC does not cover the listener's reply
port.

### firewalld exception

Test a five-minute exception restricted to the PS2's address and UDP source port.
Replace the zone and address with those reported by the diagnostic script:

```sh
sudo firewall-cmd --zone=YOUR_LAN_ZONE --timeout=5m --add-rich-rule='rule family="ipv4" source address="YOUR_PS2_IP" source-port port="12891" protocol="udp" accept'
```

Repeat discovery:

```sh
make cd-listen ARGS="--list"
```

The rule expires
automatically and does not change permanent configuration or require a firewall
reload. See firewalld's [rich rule syntax](https://firewalld.org/documentation/man-pages/firewalld.richlanguage.html)
and [rule timeouts](https://firewalld.org/documentation/man-pages/firewall-cmd.html).

If the test restores discovery, save the same PS2-only exception with:

```sh
node tools/cd/listener/allow-discovery.mjs YOUR_PS2_IP
```

This script finds the LAN zone, requests sudo to save the rule permanently, and
replaces any timed runtime copy with an untimed copy. It does not reload the
firewall. Then [start the listener](#stream-cd-audio-to-this-computer) again for
automatic discovery and playback.

## Diagnose CD dropouts

If a short fragment repeats while the console's track counter advances, record
what the PS2 sends without passing it through the PC audio system. Leave the
console and listener running during the fault, then run in another terminal:

```sh
node tools/cd/listener/diagnose-pcm.mjs YOUR_PS2_IP
```

This opens one additional audio subscription for five seconds and saves a
temporary WAV. It reports any exact cycle of half a second or less sustained in
the captured audio's last two to three seconds. Such a cycle is present in the
received data; it can also be part of the music, so it does not alone establish
a drive error. If the WAV is normal while live playback loops, investigate the
PC playback path. `--seconds 10` records longer, up to 30 seconds. The tool needs
Node.js and opens no audio device or packet capture.

For other dropouts, compare source timing with PC delivery before changing buffering; see the
[outgoing CD troubleshooting path](../../../troubleshooting.md#outgoing-cd-audio).

### Timing and counters

Every ten seconds, the listener writes timing, buffering and PS2 statistics to
the foreground terminal or the [service journal](#install-as-a-background-service-linux).
These queries work in release builds.

Reports include received frames, elapsed receive time, the
largest arrival gap, actual queued audio duration, played source frames and ring
underruns. Arrival gaps include local processing delays. Queued audio is the
listener's own reserve; PipeWire and the device can add latency. The underrun
counter measures shortages in that ring, not every possible device or server
xrun. Source diagnostics run asynchronously so waiting for UDP replies does not
pause TCP reception. The PC reserve does not prevent TCP send stalls.

PS2 source counters report submitted sectors, gaps over 40 ms, the longest
submit gap, drive-wait polls, local output-busy polls, contiguous wait maxima and
underrun silence insertions, excluding lead-out silence. These reset on manual
track changes, seeks, pauses and stops; automatic track transitions retain them.
Sender retry counts and queue maxima persist for the stream server's lifetime.
Restart the app between wired and Wi-Fi comparison runs to give those counters
matching reset points.

Queue peaks count audio sectors waiting to be sent. Opening catch-up can raise
the peak even on a healthy wired connection. A peak alone does not establish a
network stall; check arrival gaps and send-stall reports alongside it. Start
comparison runs with the same listener connection timing as well as the same
track, buffer setting and observation duration.

With diagnostics available, the listener prints `PS2 CD TCP diagnostics
available`. After a continuous send retry lasts at least 250 ms, the PS2 saves
TCP state and refreshes it at most every 250 ms while the stall continues.
`PS2 CD TCP stall` reports its duration, sample time, queued sectors, TCP send
space, unsent and unacknowledged bytes, packet reference counts, sequence
numbers, windows and retransmission timer.

The latest snapshot survives recovery and playback resets, so it can describe
an earlier session. Check whether its stall ID changes. These diagnostic queries
open no audio connection.
An unavailable TCP option is reported explicitly when the resident stack lacks
the query or the socket query fails.

Stall reports also include Ethernet transmit drops, errors, carrier losses,
excessive deferrals, collisions and FIFO underruns, plus receive allocation
failures, overruns and bad frame checksums. These console-boot counters are
sampled at the stall; compare successive snapshots for new errors. Link modes
1/2 mean 10 Mbps half/full duplex; 3/4 mean 100 Mbps half/full duplex.

The Ethernet query uses the app's socket bridge in both build variants; TCP
snapshots require the SDK's read-only socket option. See
[SDK and launcher updates](#sdk-and-launcher-updates) when updating these services.

### Capture an existing listener

Keep your listener running with the discovery and buffer settings
you want to test. In a **second terminal on the same PC**, run:

```sh
node tools/cd/listener/diagnose-stream.mjs YOUR_PS2_IP --seconds 300
```

This captures the existing audio connection without starting another listener
or opening an audio subscription. Save the listener terminal's output separately:
this mode's bundle contains the packet log and report, not the other terminal's
listener output. Your existing listener keeps running after capture finishes.

### Start a listener and capture together

Stop any existing listener and run this in **one PC terminal**:

```sh
node tools/cd/listener/diagnose-stream.mjs YOUR_PS2_IP --listen --seconds 300
```

Build with `make build-cd-listener` first. `--listen` starts
`build/cd-listener/stroom-cd-listener` with the saved listener configuration,
opens its audio subscription and captures TCP headers. Set `host` in
`listener.json` to the console being captured, or confirm that automatic
discovery selects that console. Buffering follows the file's `buffer_ms` setting.

It saves listener output alongside the packet log and
report, and stops the listener it started when capture finishes. The log retains
source timing, underruns, send retries, queue peaks, TCP stall snapshots and
Ethernet counters for wired-versus-Wi-Fi comparisons.

Both capture modes require Linux, `ip`, `tcpdump` and `timeout`, and request sudo
for capture. `--seconds` sets the observation duration. The script prints its
temporary directory and `.tar.gz` bundle path; it changes no network settings.

The report includes packet gaps, repeated sequence ranges, duplicate ACKs, PC
zero receive windows and tcpdump's capture/drop counts. Sequence repeats can
indicate retransmission or reordering; account for dropped capture packets when
interpreting the log. It also shows packets and the last PC ACK around the three
largest gaps.

When the TCP handshake is captured, the analyzer identifies the initial format
record by its stream sequence range and reports the wait until audio separately
from playback gaps. Packet counts still include format traffic. Captures starting
midstream retain their measured gaps and report startup timing as unavailable.

### Read a saved capture

To analyze a previous TCP header log without sudo or another playback session:

```sh
node tools/cd/listener/diagnose-stream.mjs YOUR_PS2_IP --read /path/to/packets.txt
```

Use the PS2 address recorded in that capture. This prints gap details from the
saved log without contacting the console.

### SDK and launcher updates

For changes to the resident network stack or the app's socket bridge, follow the
[SDK rebuild](../../sdk/README.md#rebuilds-and-output) and
[service installation](../../sdk/README.md#installing-rebuilt-services) workflow.
