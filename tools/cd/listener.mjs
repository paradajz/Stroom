#!/usr/bin/env node
import { parseArgs } from "node:util";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { performance } from "node:perf_hooks";
import { audio, cdStream as wire } from "../contracts/load.mjs";
import {
  discoverCdSources,
  receiveCdStream,
  queryCdTransport,
  formatCdTransport,
} from "./stream.mjs";
import { PC_BUFFER, parseBufferMs, prebufferPcm } from "./playback.mjs";

const usage = `Usage: node tools/cd/listener.mjs [options]
Find a Stroom PS2 and play its CD audio on this computer.
Requires Node.js 18+ and FFplay (included with FFmpeg).

  --host ADDRESS       Connect directly instead of discovering
  --port PORT          TCP/discovery port (default: ${wire.CD_STREAM_PORT})
  --broadcast ADDRESS  Override automatic per-interface broadcast discovery
  --buffer-ms MS       PC startup/recovery reserve, ${PC_BUFFER.minMs}–${PC_BUFFER.maxMs} ms (default: ${PC_BUFFER.defaultMs})
  --stats              Print input timing and PC write waits every 10 seconds
  --ffplay PATH        FFplay executable (default: ffplay)
  --list               List discovered sources and exit
  --once               Exit after one session instead of reconnecting
  --help, -h           Show help

By default, keep running and reconnect to each new playback session.
CD streaming is available automatically while CD is selected on the PS2.`;

let values, bufferMs;
try {
  ({ values } = parseArgs({
    options: {
      host: { type: "string" },
      port: { type: "string", default: String(wire.CD_STREAM_PORT) },
      broadcast: { type: "string" },
      ffplay: { type: "string", default: "ffplay" },
      "buffer-ms": { type: "string", default: String(PC_BUFFER.defaultMs) },
      stats: { type: "boolean" },
      list: { type: "boolean" },
      once: { type: "boolean" },
      help: { type: "boolean", short: "h" },
    },
    allowPositionals: false,
  }));
  if (
    !/^\d+$/.test(values.port) ||
    Number(values.port) < 1 ||
    Number(values.port) > 65535
  )
    throw Error("Invalid port");
  bufferMs = parseBufferMs(values["buffer-ms"]);
  if (values.host !== undefined && !values.host.trim())
    throw Error("Empty host");
} catch (error) {
  console.error(error.message + "\n" + usage);
  process.exit(1);
}
if (values.help) {
  console.log(usage);
} else {
  const controller = new AbortController();
  let player;
  let stopping = false;
  const stop = (code) => {
    stopping = true;
    process.exitCode = code;
    controller.abort();
    player?.kill();
  };
  process.once("SIGINT", () => stop(130));
  process.once("SIGTERM", () => stop(143));
  const delay = (ms) =>
    new Promise((resolve) => {
      if (controller.signal.aborted) {
        resolve();
        return;
      }
      const abort = () => {
        clearTimeout(timer);
        resolve();
      };
      const timer = setTimeout(() => {
        controller.signal.removeEventListener("abort", abort);
        resolve();
      }, ms);
      controller.signal.addEventListener("abort", abort, { once: true });
    });
  let previousMessage = "";
  let discoveredSource;
  function log(message) {
    if (message !== previousMessage) console.error(message);
    previousMessage = message;
  }
  async function play(source) {
    const session = new AbortController();
    const abort = () => session.abort(controller.signal.reason);
    controller.signal.addEventListener("abort", abort, { once: true });
    if (controller.signal.aborted) abort();
    let exit,
      failure,
      ended = false,
      rebuffering = false;
    function playerStartFailed(error) {
      failure = Object.assign(
        Error(`Cannot start FFplay "${values.ffplay}": ${error.message}`, {
          cause: error,
        }),
        {
          code: error.code,
          playerSetupError: ["ENOENT", "EACCES", "EPERM", "ENOTDIR"].includes(
            error.code,
          ),
        },
      );
      session.abort(failure);
      return failure;
    }
    let firstPcm,
      lastPcm,
      pcmBytes = 0,
      maxGap = 0,
      maxWriteWait = 0;
    const bytesPerSecond =
      (audio.AUDIO_RATE * wire.CD_STREAM_CHANNELS * wire.CD_STREAM_BITS) / 8;
    const reportTiming = () => {
      if (firstPcm === undefined) return;
      console.error(
        "CD timing: received " +
          (pcmBytes / bytesPerSecond).toFixed(3) +
          " s audio in " +
          ((performance.now() - firstPcm) / 1000).toFixed(3) +
          " s; max arrival gap " +
          maxGap.toFixed(1) +
          " ms; max PC write wait " +
          maxWriteWait.toFixed(1) +
          " ms",
      );
    };
    let reportingSender = false;
    let previousTransport = "";
    let sessionActive = true;
    async function reportSenderTiming() {
      if (reportingSender || stopping || !sessionActive) return;
      reportingSender = true;
      try {
        const [replies, trace] = await Promise.all([
          discoverCdSources({
            broadcast: source.host,
            port: source.port,
            timeout: 200,
            signal: controller.signal,
            includeStats: true,
          }),
          queryCdTransport(source, { signal: controller.signal }),
        ]);
        if (trace && !stopping && sessionActive) {
          const message = formatCdTransport(trace);
          if (message !== previousTransport) console.error(message);
          previousTransport = message;
        }
        const stats = replies.find(
          (reply) => reply.port === source.port,
        )?.stats;
        const producer = stats?.source;
        if (producer && !stopping && sessionActive)
          console.error(
            "PS2 CD source (since playback reset): " +
              producer.blocks +
              " sectors; max submit gap " +
              producer.max_gap_ms +
              " ms; gaps >40 ms " +
              producer.gaps +
              "; drive-wait polls " +
              producer.drive_waits +
              " (max " +
              producer.max_drive_wait_ms +
              " ms); output-busy polls " +
              producer.output_busy +
              " (max " +
              producer.max_output_busy_ms +
              " ms); underrun silence blocks " +
              producer.silence_blocks +
              "; lifetime send retries " +
              stats.send_retries +
              "; lifetime max queue " +
              stats.max_lag_blocks,
          );
      } catch {
        // Older consoles or a missing diagnostic reply must not interrupt playback.
      } finally {
        reportingSender = false;
      }
    }
    async function* incoming() {
      for await (const record of receiveCdStream(source, {
        signal: session.signal,
      })) {
        if (record.type === "pcm") {
          const now = performance.now();
          if (lastPcm !== undefined) {
            const gap = now - lastPcm;
            maxGap = Math.max(maxGap, gap);
            if (gap >= 100 && values.stats) {
              console.error(
                `PC audio arrival gap ${gap.toFixed(1)} ms at ${new Date().toISOString()}; received ${pcmBytes} bytes`,
              );
            }
          } else if (bufferMs > 0)
            log("Buffering " + bufferMs + " ms of CD audio...");
          firstPcm ??= now;
          lastPcm = now;
          pcmBytes += record.data.length;
        }
        yield record;
      }
    }
    const timing = values.stats
      ? setInterval(() => {
          reportTiming();
          void reportSenderTiming();
        }, 10000)
      : undefined;
    try {
      for await (const record of prebufferPcm(incoming(), bufferMs, {
        onRebuffer: () => {
          rebuffering = true;
          log(`Rebuffering ${bufferMs} ms of CD audio after a delivery gap...`);
        },
      })) {
        if (record.type === "format") {
          log(
            `Connected to ${source.host}:${source.port}; waiting for CD audio...`,
          );
        } else if (record.type === "pcm") {
          if (rebuffering) {
            log("CD audio ready; resuming playback.");
            rebuffering = false;
          }
          if (!player) {
            log(`Listening to ${source.host}:${source.port}`);
            try {
              player = spawn(
                values.ffplay,
                [
                  "-nodisp",
                  "-autoexit",
                  "-loglevel",
                  "warning",
                  "-f",
                  "s16le",
                  "-ar",
                  String(audio.AUDIO_RATE),
                  "-ch_layout",
                  "stereo",
                  "-probesize",
                  "32",
                  "-analyzeduration",
                  "0",
                  "-i",
                  "pipe:0",
                ],
                {
                  stdio: ["pipe", "ignore", "inherit"],
                },
              );
            } catch (error) {
              throw playerStartFailed(error);
            }
            player.stdin.on("error", (error) => {
              failure ??= error;
              session.abort(failure);
            });
            exit = new Promise((resolve) => {
              player.once("error", playerStartFailed);
              player.once("close", (code) => {
                if (code !== 0 && !failure)
                  failure = Error("FFplay exited with code " + code);
                if (!ended && !stopping) {
                  failure ??= Error("FFplay exited before the stream ended");
                  session.abort(failure);
                }
                resolve();
              });
            });
          }
          if (failure || player.exitCode !== null)
            throw failure ?? Error("FFplay exited before the stream ended");
          if (!player.stdin.write(record.data)) {
            const waitStarted = performance.now();
            const timeout = setTimeout(
              () =>
                player?.stdin.destroy(Error("PC playback cannot keep pace")),
              1500,
            );
            try {
              await once(player.stdin, "drain", { signal: session.signal });
            } finally {
              clearTimeout(timeout);
              const wait = performance.now() - waitStarted;
              if (wait >= 40 && values.stats) {
                console.error(
                  `PC audio-device write wait ${wait.toFixed(1)} ms at ${new Date().toISOString()}; stdin queue ${player?.stdin.writableLength ?? 0} bytes`,
                );
              }
              maxWriteWait = Math.max(maxWriteWait, wait);
            }
          }
        } else if (record.type === "end") {
          ended = true;
          if (player) {
            player.stdin.end();
            const timeout = setTimeout(() => {
              failure ??= Error("FFplay did not finish playback");
              player?.kill("SIGKILL");
            }, 5000);
            try {
              await exit;
            } finally {
              clearTimeout(timeout);
            }
            if (failure) throw failure;
          }
        }
      }
    } catch (error) {
      throw failure ?? error;
    } finally {
      sessionActive = false;
      session.abort();
      controller.signal.removeEventListener("abort", abort);
      clearInterval(timing);
      if (values.stats && !stopping) reportTiming();
      if (player) {
        if (!ended) player.kill();
        const timeout = setTimeout(() => player?.kill("SIGKILL"), 1000);
        try {
          await exit;
        } finally {
          clearTimeout(timeout);
          player = undefined;
        }
      }
    }
  }
  do {
    let retryDelay = 200;
    let source;
    try {
      if (values.host && !values.list) {
        source = { host: values.host, port: Number(values.port) };
      } else if (discoveredSource && !values.list) {
        source = discoveredSource;
      } else {
        const sources = await discoverCdSources({
          broadcast: values.broadcast,
          port: Number(values.port),
          signal: controller.signal,
        });
        if (values.list) {
          for (const item of sources)
            console.log(`${item.name}\t${item.host}:${item.port}`);
          if (!sources.length)
            console.error(
              "No CD discovery replies received. Start CD playback on the PS2, or try --host YOUR_PS2_IP to connect directly.",
            );
          break;
        }
        if (!sources.length) throw Error("Waiting for a Stroom CD source...");
        if (sources.length > 1)
          throw Error(
            "Multiple PS2 sources found; choose --host: " +
              sources.map((item) => item.host).join(", "),
          );
        [source] = sources;
        discoveredSource = source;
      }
      await play(source);
    } catch (error) {
      if (!stopping) {
        const unavailable =
          [
            "ECONNREFUSED",
            "ETIMEDOUT",
            "EHOSTUNREACH",
            "ENETUNREACH",
            "EHOSTDOWN",
            "ENETDOWN",
          ].includes(error.code) ||
          error.message.includes("handshake timed out");
        if (unavailable) {
          discoveredSource = undefined;
          retryDelay = 1000;
        }
        const waiting = values.host
          ? `Waiting for PS2 at ${values.host}:${values.port} (start CD playback on the PS2)...`
          : "Waiting for a Stroom CD source...";
        let message = error.message;
        if (
          source &&
          [
            "Truncated CD stream",
            "CD playback interrupted",
            "read ECONNRESET",
          ].includes(message)
        ) {
          try {
            const [sources, trace] = await Promise.all([
              discoverCdSources({
                broadcast: source.host,
                port: source.port,
                timeout: 200,
                signal: controller.signal,
                includeStats: true,
              }),
              values.stats
                ? queryCdTransport(source, { signal: controller.signal })
                : null,
            ]);
            if (trace && !stopping) console.error(formatCdTransport(trace));
            const stats = sources.find(
              (item) => item.host === source.host && item.port === source.port,
            )?.stats;
            if (stats)
              message +=
                " (PS2: " +
                stats.last_disconnect +
                "; errno " +
                stats.last_error +
                "; max queue " +
                stats.max_lag_blocks +
                " sectors; max worker gap " +
                stats.max_poll_gap_ms +
                " ms; send retries " +
                stats.send_retries +
                "; mailbox overflows " +
                stats.mailbox_overflows +
                ")";
          } catch {
            // Diagnostics must not prevent reconnecting to an older or unavailable console.
          }
        }
        if (!stopping) log(unavailable && !values.once ? waiting : message);
        if (values.once) process.exitCode = 1;
        // Permanent player setup failures require a local configuration change.
        if (error.playerSetupError) {
          process.exitCode = 1;
          break;
        }
      }
    }
    if (values.once || stopping) break;
    await delay(retryDelay);
  } while (!stopping);
}
