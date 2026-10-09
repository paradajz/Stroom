import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { resolveCover } from "../../../../../tools/cd/recognition/cover.mjs";
import { prepareCover } from "../../../../../tools/cd/recognition/image.mjs";
import { coverInput, jpegSize } from "./cover_fixture.mjs";

function ppm(width, height) {
  const pixels = Buffer.alloc(width * height * 3);
  let state = 123;
  for (let i = 0; i < pixels.length; i++) {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    pixels[i] = state >>> 24;
  }
  return Buffer.concat([
    Buffer.from("P6\n" + width + " " + height + "\n255\n"),
    pixels,
  ]);
}
for (const [width, height, expected] of [
  [1400, 1000, { width: 600, height: 429 }],
  [1000, 1400, { width: 429, height: 600 }],
  [700, 700, { width: 600, height: 600 }],
  [31, 17, { width: 31, height: 17 }],
]) {
  const result = await prepareCover(ppm(width, height));
  assert.deepEqual(jpegSize(result), expected);
  assert.ok(result.length < 512 * 1024);
  console.log(width + "x" + height + ": " + result.length + " JPEG bytes");
}
const jpeg = await prepareCover(coverInput);
assert.deepEqual(jpegSize(jpeg), { width: 32, height: 16 });
assert.deepEqual(jpegSize(await prepareCover(jpeg)), { width: 32, height: 16 });
await assert.rejects(
  prepareCover(Buffer.from("invalid image")),
  (error) => error.retryable === false,
);
const originalPath = process.env.PATH;
try {
  process.env.PATH = "/nonexistent";
  await assert.rejects(prepareCover(coverInput), /ffmpeg on PATH/);
} finally {
  process.env.PATH = originalPath;
}
const originalFetch = globalThis.fetch;
const directory = await mkdtemp("/tmp/stroom-cover-size-");
// Exercise encoded-size overflow even if the real encoder's output already fits.
const encoder = await mkdtemp("/tmp/stroom-cover-encoder-");
try {
  await writeFile(
    join(encoder, "ffmpeg"),
    `#!${process.execPath}
const {readFileSync, writeFileSync, existsSync} = require('node:fs');
const root = ${JSON.stringify(encoder)};
const count = Number(existsSync(root + '/count') ? readFileSync(root + '/count', 'utf8') : 0);
writeFileSync(root + '/count', String(count + 1));
process.stdin.resume();
process.stdout.end(count === 0 || existsSync(root + '/always') ? Buffer.alloc(600000) : Buffer.from(${JSON.stringify(jpeg.toString("base64"))}, 'base64'));
`,
    { mode: 0o755 },
  );
  process.env.PATH = encoder + ":" + originalPath;
  const smaller = await prepareCover(coverInput);
  assert.deepEqual(smaller, jpeg);
  assert.ok(Number(await readFile(join(encoder, "count"), "utf8")) > 1);
  await writeFile(join(encoder, "always"), "");
  await assert.rejects(
    prepareCover(coverInput),
    (error) => error.retryable === false && /512 KiB/.test(error.message),
  );
} finally {
  process.env.PATH = originalPath;
  await rm(encoder, { recursive: true, force: true });
}
const options = { userAgent: "test", directory };
const seed = { artist: "Artist", title: "Album" };
const report = () => ({
  discId: "size-test",
  toc: { offsets: [150], leadout: 4650 },
  selected: { releaseId: "release", mediumPosition: 1 },
  candidates: [
    {
      ...seed,
      id: "release",
      source: "musicbrainz",
      coverUrl: "https://cover-size.test/image",
      media: [
        { position: 1, tracks: [{ title: "Song", milliseconds: 60000 }] },
      ],
    },
  ],
  discogs: {
    raw: [false, true].map((relaxed) => ({
      search: seed,
      relaxed,
      response: { results: [] },
    })),
  },
});
try {
  // Original images up to 5 MiB are resized to the prepared-cover limits.
  const padded = Buffer.alloc(5 * 1024 * 1024);
  coverInput.copy(padded);
  globalThis.fetch = async () =>
    new Response(padded, { headers: { "content-type": "image/png" } });
  const accepted = report();
  await resolveCover(accepted, options);
  assert.ok(accepted.candidates[0].coverFile);
  const resized = await readFile(
    join(directory, accepted.candidates[0].coverFile),
  );
  assert.deepEqual(jpegSize(resized), { width: 32, height: 16 });
  assert.ok(resized.length < 512 * 1024);

  let downloads = 0;
  globalThis.fetch = async () => {
    downloads++;
    return new Response(
      new ReadableStream({
        start(controller) {
          controller.enqueue(padded);
          controller.enqueue(new Uint8Array(1));
          controller.close();
        },
      }),
      { headers: { "content-type": "image/png" } },
    );
  };
  const rejected = report();
  await resolveCover(rejected, options);
  assert.match(rejected.candidates[0].coverError, /exceeds 5 MiB/);
  assert.equal(rejected.candidates[0].coverAvailable, false);
  assert.equal(rejected.artworkRetryable, false);
  await resolveCover(rejected, options);
  assert.equal(
    downloads,
    1,
    "Do not download a permanently oversized image again",
  );

  const fallback = report();
  fallback.candidates.push({
    ...structuredClone(fallback.candidates[0]),
    id: "alternative",
    coverUrl: "https://alternative-size.test/image",
  });
  const oversized = globalThis.fetch;
  globalThis.fetch = async (url) =>
    String(url).includes("alternative-size")
      ? new Response(coverInput, { headers: { "content-type": "image/png" } })
      : oversized();
  await resolveCover(fallback, options);
  assert.ok(
    fallback.candidates[0].coverFile,
    "Oversized originals still allow another edition's cover",
  );
  assert.equal(fallback.artworkRetryable, false);
} finally {
  globalThis.fetch = originalFetch;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: PNG/JPEG conversion, baseline output, aspect ratio, no enlargement, malformed input and missing FFmpeg.",
);
