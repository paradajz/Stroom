import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
export const coverInput = await readFile(
  new URL("./fixtures/cover.png", import.meta.url),
);

export function jpegSize(input) {
  const bytes = Buffer.from(input);
  assert.equal(bytes.readUInt16BE(0), 0xffd8);
  for (let offset = 2; offset + 4 < bytes.length;) {
    assert.equal(bytes[offset], 0xff);
    const marker = bytes[offset + 1];
    const length = bytes.readUInt16BE(offset + 2);
    if (marker === 0xc0 || marker === 0xc2) {
      assert.equal(marker, 0xc0, "PS2 receives baseline JPEG");
      assert.equal(bytes[offset + 4], 8);
      assert.equal(bytes[offset + 9], 3);
      return {
        width: bytes.readUInt16BE(offset + 7),
        height: bytes.readUInt16BE(offset + 5),
      };
    }
    assert.ok(length >= 2);
    offset += length + 2;
  }
  assert.fail("JPEG dimensions missing");
}
export function assertCover(bytes) {
  assert.deepEqual(jpegSize(bytes), { width: 32, height: 16 });
}
