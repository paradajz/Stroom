import assert from "node:assert/strict";
import {
  lookupResponse,
  pendingResponse,
  sendJson,
  MAX_JSON_BYTES,
} from "../../../../../tools/cd/recognition/api.mjs";
import { parseToc } from "../../../../../tools/cd/recognition/disc.mjs";

const toc = { type: "stroom-cd-toc", first: 1, offsets: [150], leadout: 300 };
for (const extra of [
  ',"first":1',
  ',"f\\u0069rst":1',
  ',"extra":{"a":1,"a":2}',
  ',"extra":{"é":1,"\\u00e9":2}',
])
  assert.throws(
    () => parseToc(Buffer.from(JSON.stringify(toc).slice(0, -1) + extra + "}")),
    /Duplicate JSON key/,
  );
assert.deepEqual(
  parseToc(
    Buffer.from(
      JSON.stringify({ ...toc, extra: [{ a: 1 }, { a: 2 }, '{"a":1,"a":2}'] }),
    ),
  ),
  toc,
);

const id = "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-";
assert.equal(MAX_JSON_BYTES, 65536);
assert.equal(pendingResponse(id).body.result_path, "/cd/v2/lookups/" + id);
for (const label of [
  '"'.repeat(127),
  "\\".repeat(127),
  "\u0001".repeat(127),
  "🎵".repeat(32),
]) {
  const report = {
    discId: id,
    toc: { offsets: Array(99).fill(150) },
    selected: { releaseId: "release", mediumPosition: 1 },
    candidates: [
      {
        id: "release",
        title: label,
        artist: label,
        media: [
          {
            position: 1,
            tracks: Array.from({ length: 99 }, () => ({
              title: label,
              artist: label,
            })),
          },
        ],
      },
    ],
    artworkRetryable: true,
  };
  const response = lookupResponse(report);
  let bytes;
  sendJson(
    {
      writeHead(status, headers) {
        assert.equal(status, 200);
        assert.ok(headers["Content-Length"] <= MAX_JSON_BYTES);
        return this;
      },
      end(value) {
        bytes = value;
      },
    },
    response,
  );
  const body = JSON.parse(bytes);
  assert.equal(body.tracks.length, 99);
  assert.equal(body.artwork.status_path, undefined);
  for (const value of [
    body.album,
    body.artist,
    ...body.tracks.flatMap((t) => [t.title, t.artist]),
  ]) {
    assert.ok(Buffer.byteLength(value) <= 127);
    assert.doesNotMatch(value, /[\u0000-\u001f]/);
    assert.equal(value, value.toWellFormed());
  }
  if (label.startsWith("\u0001")) assert.equal(body.album, " ".repeat(127));
  if (label.startsWith("🎵")) assert.ok(bytes.includes(Buffer.from("🎵")));
}
console.log(
  "PASS: bounded 99-track JSON, control sanitization, UTF-8 boundaries, body-only artwork delays and JSON result path.",
);
