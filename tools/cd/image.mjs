import { execFile } from "node:child_process";
import { artwork } from "../contracts/load.mjs";
const { ARTWORK_MAX_BYTES } = artwork;

const COVER_SIDE = 700;
// FFmpeg uses a quantizer scale, not a percentage; 5 approximates quality 80.
const JPEG_QSCALE = 5;
const CONVERSION_TIMEOUT_MS = 15000;

/** Prepare one baseline JPEG without cropping or enlarging the source. */
export function prepareCover(bytes) {
  return new Promise((resolve, reject) => {
    const child = execFile(
      "ffmpeg",
      [
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-protocol_whitelist",
        "pipe",
        "-i",
        "pipe:0",
        "-map",
        "0:v:0",
        "-frames:v",
        "1",
        "-an",
        "-vf",
        "scale=w='min(" +
          COVER_SIDE +
          ",iw)':h='min(" +
          COVER_SIDE +
          ",ih)':force_original_aspect_ratio=decrease,setsar=1",
        "-map_metadata",
        "-1",
        "-c:v",
        "mjpeg",
        "-q:v",
        String(JPEG_QSCALE),
        "-pix_fmt",
        "yuvj420p",
        "-f",
        "image2pipe",
        "pipe:1",
      ],
      {
        encoding: "buffer",
        timeout: CONVERSION_TIMEOUT_MS,
        maxBuffer: ARTWORK_MAX_BYTES,
      },
      (error, stdout, stderr) => {
        if (error || !stdout.length) {
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
    // FFmpeg can reject malformed input before consuming the entire pipe.
    child.stdin.on("error", () => {});
    child.stdin.end(bytes);
  });
}
