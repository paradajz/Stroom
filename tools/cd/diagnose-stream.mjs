#!/usr/bin/env node
import { spawn, spawnSync } from "node:child_process";
import { mkdtemp, readFile, writeFile, appendFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { isIPv4 } from "node:net";
import { parseArgs } from "node:util";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import { cdStream as wire } from "../contracts/load.mjs";
import { queryCdTransport, formatCdTransport } from "./stream.mjs";
import {
  streamTcpDiagnostics,
  streamTcpGapDetails,
} from "./tcp-diagnostics.mjs";

const usage = `Usage: node tools/cd/diagnose-stream.mjs PS2_IP [--seconds 120] [--listen]
       node tools/cd/diagnose-stream.mjs PS2_IP --read /path/to/packets.txt
Without --listen: capture the existing connection from a second PC terminal;
keep the listener running and save its terminal output separately.
With --listen: stop your existing listener first; this starts one with --stats,
saves its output with the capture, and stops it when capture ends.
With --read: analyze saved headers offline, without sudo or a console connection.
Live capture requires Linux, iproute2, tcpdump and timeout, and requests sudo.
Saves TCP headers, comparison counters and a report in a temporary directory.`;

let capture, completion, listener, directory;
let stopping = false;
let listenerDone = Promise.resolve();
let listenerLog = Promise.resolve();
async function stopListener() {
  if (!listener) return;
  listener.kill("SIGINT");
  const timer = setTimeout(() => listener?.kill("SIGKILL"), 2000);
  try {
    await listenerDone;
    await listenerLog;
  } finally {
    clearTimeout(timer);
    listener = undefined;
  }
}
const stop = () => {
  stopping = true;
  capture?.kill("SIGINT");
  listener?.kill("SIGINT");
};

try {
  const { values, positionals } = parseArgs({
    options: {
      seconds: { type: "string", default: "120" },
      read: { type: "string" },
      listen: { type: "boolean" },
      help: { type: "boolean", short: "h" },
    },
    allowPositionals: true,
  });
  if (values.help) {
    console.log(usage);
  } else {
    const [host] = positionals;
    if (
      positionals.length !== 1 ||
      !isIPv4(host ?? "") ||
      !/^\d+$/.test(values.seconds) ||
      Number(values.seconds) < 1 ||
      Number(values.seconds) > 600
    )
      throw Error(usage);
    const analyzer = streamTcpDiagnostics(host, wire.CD_STREAM_PORT);
    if (values.read !== undefined) {
      const lines = (await readFile(values.read, "utf8")).split(/\r?\n/);
      for (const line of lines) analyzer.observe(line);
      console.log(
        `Saved capture: ${values.read}; TCP packets: ${analyzer.totals.packets}`,
      );
      console.log(streamTcpGapDetails(lines, analyzer));
    } else {
      const route = spawnSync("ip", ["-j", "-4", "route", "get", host], {
        encoding: "utf8",
      });
      if (route.error || route.status !== 0)
        throw Error("Cannot read the PS2 route; install iproute2 on Linux.");
      const [path] = JSON.parse(route.stdout);
      if (!path?.dev) throw Error("No interface found for the PS2 route.");
      for (const command of ["tcpdump", "timeout"]) {
        const check = spawnSync(command, ["--version"], { stdio: "ignore" });
        if (check.error || check.status !== 0)
          throw Error(`Install ${command} and rerun this script.`);
      }
      const root = process.getuid?.() === 0;
      if (!root) {
        console.log("Authorizing capture of the existing audio connection...");
        const authorization = spawnSync("sudo", ["-v"], { stdio: "inherit" });
        if (authorization.error || authorization.status !== 0)
          throw Error("Packet capture authorization failed.");
      }
      directory = await mkdtemp(join(tmpdir(), "stroom-cd-tcp-"));
      process.once("SIGINT", stop);
      process.once("SIGTERM", stop);
      console.log(
        `Interface: ${path.dev}; PS2: ${host}:${wire.CD_STREAM_PORT}`,
      );
      console.log(
        `Watching for ${values.seconds} seconds. Keep audio playing; reproduce a dropout.`,
      );
      console.log(`TCP header log: ${join(directory, "packets.txt")}`);
      if (values.listen) {
        console.log("Starting the audio listener in this terminal...");
        listener = spawn(
          process.execPath,
          [
            fileURLToPath(new URL("./listener.mjs", import.meta.url)),
            "--host",
            host,
            "--stats",
          ],
          { stdio: ["ignore", "pipe", "pipe"] },
        );
        for (const stream of [listener.stdout, listener.stderr]) {
          stream.on("data", (data) => {
            process.stdout.write(data);
            listenerLog = listenerLog
              .then(() => appendFile(join(directory, "listener.txt"), data))
              .catch((error) =>
                console.error(`Listener log failed: ${error.message}`),
              );
          });
        }
        listenerDone = new Promise((resolve) => {
          listener.once("close", resolve);
          listener.once("error", resolve);
        });
        listener.on("error", (error) =>
          console.error(`Listener failed: ${error.message}`),
        );
      }
      const args = [
        "timeout",
        "--signal=INT",
        values.seconds,
        "tcpdump",
        "-l",
        "-tt",
        "-nn",
        "-S",
        "-s",
        "128",
        "-i",
        path.dev,
        `tcp and host ${host} and port ${wire.CD_STREAM_PORT}`,
      ];
      capture = spawn(root ? args[0] : "sudo", root ? args.slice(1) : args, {
        stdio: ["ignore", "pipe", "pipe"],
      });
      completion = new Promise((resolve, reject) => {
        capture.once("error", reject);
        capture.once("close", (code, signal) => resolve({ code, signal }));
      });
      completion.catch(() => {});
      let errors = "";
      capture.stderr.on("data", (data) => {
        errors += data.toString();
      });
      const packets = [];
      let events = 0;
      for await (const line of createInterface({ input: capture.stdout })) {
        packets.push(line);
        for (const event of analyzer.observe(line)) {
          if (++events <= 30) console.log(event);
        }
      }
      const result = await completion;
      await stopListener();
      await writeFile(
        join(directory, "packets.txt"),
        packets.join("\n") + "\n" + errors,
      );
      if (!stopping && result.code !== 0 && result.code !== 124)
        throw Error(`Packet capture failed: ${errors.trim() || result.code}`);
      const t = analyzer.totals;
      const summary = [
        `Captured TCP packets: ${t.packets}; PS2 data packets: ${t.dataPackets}`,
        `Largest gap between PS2 data packets (${t.startupSessions ? "observed startup excluded" : "startup not identified"}): ${t.maxGapMs.toFixed(1)} ms`,
        `Repeated/overlapping PS2 sequence ranges: ${t.repeats} (retransmission candidates)`,
        `Repeated PC ACKs with unchanged window: ${t.duplicateAcks}`,
        `PC zero-window advertisements: ${t.zeroWindows}`,
        "Sequence repeats can also reflect packet reordering; inspect the header log to confirm retransmissions.",
        errors.trim(),
        `Header log: ${join(directory, "packets.txt")}`,
      ].join("\n");
      const trace = await queryCdTransport({ host, port: wire.CD_STREAM_PORT });
      const report =
        [
          summary,
          streamTcpGapDetails(packets, analyzer),
          trace ? formatCdTransport(trace) : "PS2 TCP summary unavailable.",
        ].join("\n\n") + "\n";
      await writeFile(join(directory, "report.txt"), report);
      console.log("\n" + report);
      console.log(`Combined report: ${join(directory, "report.txt")}`);
      const archive = directory + ".tar.gz";
      const packed = spawnSync("tar", ["-czf", archive, "-C", directory, "."], {
        encoding: "utf8",
        timeout: 5000,
      });
      if (!packed.error && packed.status === 0)
        console.log(`Diagnostic bundle: ${archive}`);
      else
        console.error(
          "Could not archive diagnostics; the report directory is intact.",
        );
      if (!t.dataPackets)
        console.log(
          "No audio TCP data captured. Confirm the listener was playing on this PC.",
        );
    }
  }
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
} finally {
  await stopListener();
  if (capture?.exitCode === null && capture?.signalCode === null)
    capture.kill("SIGINT");
  if (completion) await completion.catch(() => {});
  process.removeListener("SIGINT", stop);
  process.removeListener("SIGTERM", stop);
}
