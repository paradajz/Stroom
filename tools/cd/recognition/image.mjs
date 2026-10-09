import { execFile } from "node:child_process";
import { cd } from "../../contracts/load.mjs";
import { checkSignal } from "./deadline.mjs";
const { CD_LOOKUP_COVER_BYTES, CD_LOOKUP_COVER_SIDE } = cd;

// FFmpeg uses a quantizer scale, not a percentage; 5 approximates quality 80.
const JPEG_QSCALE = 5;
const CONVERSION_TIMEOUT_MS = 15000;

/** Read dimensions of a bounded, prepared baseline JPEG. */
export function coverDimensions(bytes) {
  if (
    bytes.length < 4 ||
    bytes.readUInt16BE(0) !== 0xffd8 ||
    bytes.readUInt16BE(bytes.length - 2) !== 0xffd9
  )
    return null;
  for (let at = 2; at + 4 < bytes.length;) {
    if (bytes[at++] !== 0xff) return null;
    while (bytes[at] === 0xff) at++;
    if (at + 3 > bytes.length) return null;
    const marker = bytes[at++];
    const length = bytes.readUInt16BE(at);
    if (length < 2 || at + length > bytes.length) return null;
    if (marker === 0xc0 && length >= 8 && bytes[at + 2] === 8) {
      const height = bytes.readUInt16BE(at + 3),
        width = bytes.readUInt16BE(at + 5);
      return width && height ? { width, height } : null;
    }
    if (marker === 0xda || marker === 0xd9) return null;
    at += length;
  }
  return null;
}

/** Enforce both dimensions and encoded bytes within one conversion deadline. */
export async function prepareCover(bytes, { signal } = {}) {
  const until = performance.now() + CONVERSION_TIMEOUT_MS;
  for (const [side, quality] of [
    [CD_LOOKUP_COVER_SIDE, JPEG_QSCALE],
    [CD_LOOKUP_COVER_SIDE, 10],
    [CD_LOOKUP_COVER_SIDE, 20],
    [450, 20],
    [300, 20],
  ]) {
    checkSignal(signal);
    const remaining = Math.floor(until - performance.now());
    if (remaining <= 0)
      throw Object.assign(Error("Cover conversion timed out"), {
        retryable: true,
      });
    try {
      return await convert(bytes, side, quality, remaining, signal);
    } catch (error) {
      if (!error.oversized) throw error;
    }
  }
  throw Object.assign(Error("Prepared cover exceeds 512 KiB"), {
    retryable: false,
  });
}

function convert(bytes, side, quality, timeout, signal) {
  return new Promise((resolve, reject) => {
    checkSignal(signal);
    const abort = () => child.kill("SIGKILL");
    const child = execFile(
      "ffmpeg",
      [
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-filter_threads",
        "1",
        "-protocol_whitelist",
        "pipe",
        "-threads",
        "2",
        "-i",
        "pipe:0",
        "-map",
        "0:v:0",
        "-frames:v",
        "1",
        "-an",
        "-vf",
        "scale=w='min(" +
          side +
          ",iw)':h='min(" +
          side +
          ",ih)':force_original_aspect_ratio=decrease,setsar=1",
        "-map_metadata",
        "-1",
        "-c:v",
        "mjpeg",
        "-threads",
        "2",
        "-q:v",
        String(quality),
        "-pix_fmt",
        "yuvj420p",
        "-f",
        "image2pipe",
        "pipe:1",
      ],
      {
        encoding: "buffer",
        timeout,
        killSignal: "SIGKILL",
        maxBuffer: CD_LOOKUP_COVER_BYTES + 1,
      },
      (error, stdout, stderr) => {
        signal?.removeEventListener("abort", abort);
        try {
          checkSignal(signal);
        } catch (cancelled) {
          reject(cancelled);
          return;
        }
        if (
          error?.code === "ERR_CHILD_PROCESS_STDIO_MAXBUFFER" ||
          stdout.length > CD_LOOKUP_COVER_BYTES
        ) {
          reject(
            Object.assign(Error("Prepared cover exceeds 512 KiB"), {
              oversized: true,
            }),
          );
        } else if (error || !stdout.length) {
          const failure = Error(
            error?.code === "ENOENT"
              ? "Cover conversion needs ffmpeg on PATH"
              : "Cover conversion failed: " +
                  (stderr.toString().trim().slice(0, 512) ||
                    error?.message ||
                    "empty JPEG"),
          );
          failure.retryable =
            error?.code === "ENOENT" || Boolean(error?.killed);
          reject(failure);
        } else resolve(stdout);
      },
    );
    // execFile's callback runs after the child closes. Await that cleanup before
    // releasing the worker, even when cancellation has already killed FFmpeg.
    signal?.addEventListener("abort", abort, { once: true });
    if (signal?.aborted) abort();
    // FFmpeg can reject malformed input before consuming the entire pipe.
    child.stdin.on("error", () => {});
    child.stdin.end(bytes);
  });
}
