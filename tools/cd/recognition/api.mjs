import { cd, metadata } from "../../contracts/load.mjs";
import { parseToc } from "./disc.mjs";

export const ID_PATTERN = /^[A-Za-z0-9._]{27}-$/;
export const MAX_JSON_BYTES = cd.CD_LOOKUP_RESPONSE_BYTES;
const seconds = (at, fallback = 5) =>
  Math.min(
    cd.CD_LOOKUP_MAX_RETRY_SECONDS,
    Math.max(1, Math.ceil((at - Date.now()) / 1000) || fallback),
  );

function text(value) {
  let result = "";
  for (let char of String(value ?? "").toWellFormed()) {
    if (char.codePointAt(0) < 32) char = " ";
    if (Buffer.byteLength(result + char) >= metadata.METADATA_TEXT_BYTES) break;
    result += char;
  }
  return result;
}

export function artworkState(report, serving = true) {
  const winner = report.candidates?.find(
    (item) => item.id === report.selected?.releaseId,
  );
  if (
    serving &&
    typeof winner?.coverFile === "string" &&
    winner.coverFile.startsWith(report.discId + "-") &&
    /^[A-Za-z0-9._-]+\.jpg$/.test(winner.coverFile)
  )
    return { status: "available", path: "/covers/" + winner.coverFile };
  if (serving && report.artworkRetryable)
    return {
      status: "pending",
      retry_after_seconds: seconds(report.artworkRetryAt),
    };
  return { status: "unavailable" };
}

export function pendingResponse(id) {
  const path = "/cd/v2/lookups/" + id;
  return {
    status: 202,
    retry: 5,
    body: {
      protocol: 2,
      disc_id: id,
      status: "pending",
      result_path: path,
      retry_after_seconds: 5,
    },
  };
}

export function apiError(status, code, message, retry, id) {
  const retryable = [429, 502, 503].includes(status);
  retry = retryable
    ? Math.min(
        cd.CD_LOOKUP_MAX_RETRY_SECONDS,
        Math.max(1, Math.ceil(retry ?? 15)),
      )
    : undefined;
  return {
    status,
    retry,
    body: {
      protocol: 2,
      ...(id ? { disc_id: id } : {}),
      status: "error",
      error: { code, message, retryable },
      ...(retry ? { retry_after_seconds: retry } : {}),
    },
  };
}

export function lookupResponse(report, serving = true) {
  if (!report.selected) {
    if (report.lookupStatus === "no-match")
      return {
        status: 200,
        body: {
          protocol: 2,
          disc_id: report.discId,
          status: "no_match",
          expires_in_seconds: seconds(report.lookupRetryAt),
        },
      };
    return apiError(
      502,
      "provider_failure",
      "Recognition provider failed",
      seconds(report.lookupRetryAt, 60),
      report.discId,
    );
  }
  const winner = report.candidates.find(
    (item) => item.id === report.selected.releaseId,
  );
  const medium = winner?.media.find(
    (item) => item.position === report.selected.mediumPosition,
  );
  if (!medium || medium.tracks.length !== report.toc.offsets.length)
    throw Error("Invalid selected disc");
  return {
    status: 200,
    body: {
      protocol: 2,
      disc_id: report.discId,
      status: "matched",
      track_count: medium.tracks.length,
      album: text(winner.title),
      artist: text(winner.artist),
      tracks: medium.tracks.map((track, i) => ({
        number: i + 1,
        title: text(track.title),
        artist: text(track.artist),
      })),
      artwork: artworkState(report, serving),
    },
  };
}

export function sendJson(res, response, extra = {}) {
  if (res.destroyed) return;
  const bytes = Buffer.from(JSON.stringify(response.body));
  if (bytes.length > MAX_JSON_BYTES) throw Error("API response too large");
  res
    .writeHead(response.status, {
      "Content-Type": "application/json; charset=utf-8",
      "Content-Length": bytes.length,
      "Cache-Control": "no-store",
      ...(response.retry ? { "Retry-After": String(response.retry) } : {}),
      ...response.headers,
      ...extra,
    })
    .end(bytes);
}

/** Enforce framing and byte bounds before decoding or parsing any JSON. */
export async function readTocBody(req, deadline) {
  const invalid = (status, code, message) =>
    Object.assign(Error(message), { status, code });
  if (
    req.headers["content-encoding"] &&
    req.headers["content-encoding"].toLowerCase() !== "identity"
  )
    throw invalid(
      415,
      "unsupported_media_type",
      "Request compression is unsupported",
    );
  const media = req.headers["content-type"];
  if (
    typeof media !== "string" ||
    !/^application\/json(?:\s*;\s*charset\s*=\s*(?:utf-8|"utf-8"))?\s*$/i.test(
      media,
    )
  )
    throw invalid(
      415,
      "unsupported_media_type",
      "Use application/json with UTF-8",
    );
  const length = req.headers["content-length"];
  if (!length || !/^\d+$/.test(length) || !Number.isSafeInteger(Number(length)))
    throw invalid(400, "invalid_request", "A known Content-Length is required");
  if (Number(length) > cd.CD_LOOKUP_PACKET_BYTES)
    throw invalid(413, "request_too_large", "TOC body exceeds 1024 bytes");
  if (Number(length) === 0)
    throw invalid(400, "invalid_request", "Empty TOC body");
  const timer = setTimeout(
    () => req.destroy(),
    Math.max(1, deadline - performance.now()),
  );
  timer.unref();
  try {
    const buffer = Buffer.alloc(Number(length));
    let used = 0;
    for await (const chunk of req) {
      if (used + chunk.length > buffer.length)
        throw invalid(
          413,
          "request_too_large",
          "TOC body exceeds declared size",
        );
      chunk.copy(buffer, used);
      used += chunk.length;
    }
    if (used !== buffer.length)
      throw invalid(400, "invalid_request", "Truncated TOC body");
    // A fatal decoder rejects invalid UTF-8 rather than inserting replacement characters.
    const value = new TextDecoder("utf-8", { fatal: true }).decode(buffer);
    try {
      return parseToc(Buffer.from(value));
    } catch {
      throw invalid(400, "invalid_request", "Invalid CD TOC");
    }
  } catch (error) {
    if (error.status) throw error;
    throw invalid(400, "invalid_request", "Malformed or incomplete UTF-8 body");
  } finally {
    clearTimeout(timer);
  }
}
