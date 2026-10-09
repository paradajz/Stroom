#!/usr/bin/env node
import { audio, ariacast, metadata } from "../contracts/load.mjs";
import { parseArgs } from "node:util";
import { streamPCM, validDeviceName } from "./ariacast.mjs";
import { stereoPackets } from "./pcm_input.mjs";

const usage = `Usage: node tools/aria/sender.mjs --host HOST [options]
Input: raw ${audio.AUDIO_RATE} Hz signed 16-bit little-endian PCM from standard input.

Options:
  --channels COUNT    Input channel count (default: ${ariacast.ARIA_PCM_CHANNELS})
  --left CHANNEL      Input channel for left output (default: 1)
  --right CHANNEL     Input channel for right output (default: 2)
  --listen            Animate the visualizer without PS2 sound
  --device-name NAME  Display label; requires --listen
  --help, -h          Show this help

Channel numbers start at 1.`;
let options;
try {
  options = parseArgs({
    options: {
      host: { type: "string" },
      channels: { type: "string", default: String(ariacast.ARIA_PCM_CHANNELS) },
      left: { type: "string", default: "1" },
      right: { type: "string", default: "2" },
      listen: { type: "boolean" },
      "device-name": { type: "string" },
      help: { type: "boolean", short: "h" },
    },
    allowPositionals: false,
  });
} catch (error) {
  console.error(error.message + "\n" + usage);
  process.exit(1);
}
const host = options.values.host;
const channelOptions = [
  options.values.channels,
  options.values.left,
  options.values.right,
];
const listening = options.values.listen === true;
const deviceName = options.values["device-name"];
if (options.values.help) {
  console.log(usage);
} else if (!host?.trim()) {
  console.error("--host is required.\n" + usage);
  process.exitCode = 1;
} else if (!channelOptions.every((value) => /^\d+$/.test(value))) {
  console.error("Channel options must be positive whole numbers.\n" + usage);
  process.exitCode = 1;
} else if (
  deviceName !== undefined &&
  (!listening || !validDeviceName(deviceName))
) {
  console.error(
    usage +
      `\nDevice names require --listen and 1–${metadata.METADATA_DEVICE_NAME_MAX_BYTES} UTF-8 bytes without control characters.`,
  );
  process.exitCode = 1;
} else {
  const [channels, left, right] = channelOptions.map(Number);
  let packets;
  try {
    packets = stereoPackets(process.stdin, channels, left, right);
  } catch (error) {
    console.error(error.message + "\n" + usage);
    process.exit(1);
  }
  function stop(code) {
    stream.close();
    process.stdin.destroy();
    process.exitCode = code;
  }
  const stream = streamPCM(
    host,
    (message) => {
      console.error(message);
      stop(1);
    },
    {
      listening,
      deviceName,
      async readPacket() {
        const packet = await packets.next();
        return packet.done ? null : packet.value;
      },
      onEnd() {
        process.stdin.destroy();
      },
    },
  );
  process.on("SIGINT", () => stop(130));
  process.on("SIGTERM", () => stop(143));
  console.error(
    `Sending ${listening ? "silent listening" : "audible streaming"}: ${channels}-channel input, channels ${left}/${right}, to ${host}:${ariacast.ARIA_STREAM_PORT}`,
  );
}
