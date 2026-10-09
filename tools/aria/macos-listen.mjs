#!/usr/bin/env node
import { audio, ariacast } from "../contracts/load.mjs";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { validDeviceName } from "./ariacast.mjs";

import { serviceLog } from "./service_log.mjs";

const RETRY_MS = 5000;
const STOP_GRACE_MS = 1000;
const [ffmpeg, host, deviceName, logPath] = process.argv.slice(2);
if (
  ![5, 6].includes(process.argv.length) ||
  !ffmpeg ||
  !host ||
  !validDeviceName(deviceName)
) {
  console.error(
    "Usage: node tools/aria/macos-listen.mjs FFMPEG HOST DEVICE_NAME [LOG_PATH]",
  );
  process.exit(1);
}

const log = logPath
  ? serviceLog(logPath)
  : (data) => process.stderr.write(data);
const report = (message) => log(`${message}\n`);

const senderPath = fileURLToPath(new URL("./sender.mjs", import.meta.url));
let stopping = false;
let stopAttempt = () => {};
let cancelRetry = () => {};
for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    stopping = true;
    stopAttempt();
    cancelRetry();
  });
}

async function attempt() {
  // Own both children: failure of either must also stop a blocked peer.
  const capture = spawn(
    ffmpeg,
    [
      "-nostdin",
      "-hide_banner",
      "-nostats",
      "-loglevel",
      "error",
      "-f",
      "avfoundation",
      "-i",
      ":Scarlett 18i20 USB",
      "-af",
      "pan=stereo|c0=c8|c1=c9",
      "-ar",
      String(audio.AUDIO_RATE),
      "-ac",
      String(ariacast.ARIA_PCM_CHANNELS),
      "-c:a",
      "pcm_s16le",
      "-f",
      "s16le",
      "pipe:1",
    ],
    { stdio: ["ignore", "pipe", "pipe"] },
  );
  const sender = spawn(
    process.execPath,
    [senderPath, "--listen", "--device-name", deviceName, "--host", host],
    { stdio: ["pipe", "ignore", "pipe"] },
  );
  capture.stderr.on("data", log);
  sender.stderr.on("data", log);
  const children = new Set([capture, sender]);
  let ending = false;
  let killTimer;
  function end() {
    if (ending) return;
    ending = true;
    capture.stdout.unpipe(sender.stdin);
    sender.stdin.destroy();
    for (const child of children) child.kill("SIGTERM");
    killTimer = setTimeout(() => {
      for (const child of children) child.kill("SIGKILL");
    }, STOP_GRACE_MS);
  }
  stopAttempt = end;
  const closed = [...children].map(
    (child) =>
      new Promise((resolve) => {
        child.on("error", (error) => {
          report(error.message);
          end();
        });
        child.on("exit", end);
        child.on("close", () => {
          children.delete(child);
          end();
          resolve();
        });
      }),
  );
  sender.stdin.on("error", end);
  capture.stdout.on("error", end);
  capture.stdout.pipe(sender.stdin);
  await Promise.all(closed);
  clearTimeout(killTimer);
  stopAttempt = () => {};
}

while (!stopping) {
  await attempt();
  if (stopping) break;
  report("Listening sender will retry in 5 seconds.");
  await new Promise((resolve) => {
    const timer = setTimeout(resolve, RETRY_MS);
    cancelRetry = () => {
      clearTimeout(timer);
      resolve();
    };
  });
  cancelRetry = () => {};
}
