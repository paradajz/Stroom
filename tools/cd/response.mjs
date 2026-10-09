import { isIPv4 } from "node:net";

import { metadata, cd } from "../contracts/load.mjs";
const { METADATA_TEXT_BYTES, METADATA_ARTWORK_URL_BYTES } = metadata;

function bounded(value, bytes) {
  let result = "";
  for (const char of String(value ?? "")) {
    if (Buffer.byteLength(result + char) > bytes) break;
    result += char;
  }
  return result;
}

/** Same metadata document as AriaCast, prefixed by a CD request correlation and artwork-state header. */
export function metadataReply(report, request, port, artworkServing = true) {
  if (
    !report.selected ||
    typeof request.service !== "string" ||
    !Number.isInteger(request.request) ||
    request.request < 1 ||
    request.request > 0xffffffff ||
    !Number.isInteger(request.track) ||
    request.track < 1 ||
    request.track > report.toc.offsets.length ||
    !isIPv4(request.service)
  )
    return null;
  const winner = report.candidates.find(
    (item) => item.id === report.selected?.releaseId,
  );
  const medium = winner?.media.find(
    (item) => item.position === report.selected.mediumPosition,
  );
  const track = medium?.tracks[request.track - 1];
  if (!track) return null;
  // Listener failure affects delivery only; keep the cached cover intact.
  const coverAvailable = artworkServing && Boolean(winner.coverFile);
  const data = {
    title: bounded(track.title, METADATA_TEXT_BYTES - 1),
    artist: bounded(track.artist || winner.artist, METADATA_TEXT_BYTES - 1),
    album: bounded(winner.title, METADATA_TEXT_BYTES - 1),
    artwork_url: coverAvailable
      ? bounded(
          `http://${request.service}:${port}/covers/${encodeURIComponent(winner.coverFile)}`,
          METADATA_ARTWORK_URL_BYTES - 1,
        )
      : "",
  };
  const header = Buffer.alloc(cd.CD_LOOKUP_REPLY_HEADER_BYTES);
  header.write(cd.CD_LOOKUP_REPLY_MAGIC);
  header.writeUInt32BE(request.request, cd.CD_LOOKUP_TOKEN_OFFSET);
  header[cd.CD_LOOKUP_ARTWORK_STATE_OFFSET] = coverAvailable
    ? cd.CD_LOOKUP_ARTWORK_AVAILABLE
    : !artworkServing || report.artworkRetryable === false
      ? cd.CD_LOOKUP_ARTWORK_UNAVAILABLE
      : cd.CD_LOOKUP_ARTWORK_PENDING;
  return Buffer.concat([
    header,
    Buffer.from(JSON.stringify({ type: "metadata", data })),
  ]);
}

/** Correlated overload reply; older clients ignore its distinct magic. */
export function busyReply(token, retryMs = 15000) {
  const packet = Buffer.alloc(cd.CD_LOOKUP_BUSY_REPLY_BYTES);
  packet.write(cd.CD_LOOKUP_BUSY_MAGIC);
  packet.writeUInt32BE(token, cd.CD_LOOKUP_TOKEN_OFFSET);
  packet.writeUInt32BE(retryMs, cd.CD_LOOKUP_BUSY_RETRY_OFFSET);
  return packet;
}
