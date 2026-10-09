import assert from "node:assert/strict";
import {
  mkdtempSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  existsSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  serviceLog,
  SERVICE_LOG_BYTES,
} from "../../../../../tools/aria/service_log.mjs";

const dir = mkdtempSync(join(tmpdir(), "stroom-log-"));
try {
  const path = join(dir, "sender.log");
  let log = serviceLog(path);
  let history = Buffer.alloc(0);
  for (const chunk of [
    Buffer.from("Connecting\n"),
    Buffer.alloc(SERVICE_LOG_BYTES, "a"),
    Buffer.alloc(3 * SERVICE_LOG_BYTES + 17, "b"),
    Buffer.from("Reconnected\n"),
  ]) {
    log(chunk);
    history = Buffer.concat([history, chunk]);
    const current = readFileSync(path);
    const backup = existsSync(`${path}.1`)
      ? readFileSync(`${path}.1`)
      : Buffer.alloc(0);
    assert.ok(current.length <= SERVICE_LOG_BYTES);
    assert.ok(backup.length <= SERVICE_LOG_BYTES);
    const retained = Buffer.concat([backup, current]);
    assert.deepEqual(retained, history.subarray(-retained.length));
    // A service restart must preserve the current rotation position.
    log = serviceLog(path);
  }

  // Upgrade both oversized files without retaining their old unbounded sizes.
  const oversized = Buffer.concat([
    Buffer.alloc(SERVICE_LOG_BYTES, "old"),
    Buffer.alloc(SERVICE_LOG_BYTES, "recent"),
  ]);
  writeFileSync(path, oversized);
  writeFileSync(`${path}.1`, oversized);
  log = serviceLog(path);
  assert.deepEqual(readFileSync(path), oversized.subarray(-SERVICE_LOG_BYTES));
  assert.deepEqual(
    readFileSync(`${path}.1`),
    oversized.subarray(-SERVICE_LOG_BYTES),
  );
  log("After upgrade\n");
  assert.equal(readFileSync(path, "utf8"), "After upgrade\n");
  assert.deepEqual(
    readFileSync(`${path}.1`),
    oversized.subarray(-SERVICE_LOG_BYTES),
  );
  // A bad destination must neither throw nor prevent later recovery.
  rmSync(path);
  mkdirSync(path);
  log = serviceLog(path);
  assert.doesNotThrow(() => log("cannot write"));
  assert.doesNotThrow(() => log("still unavailable"));
  rmSync(path, { recursive: true });
  log("Recovered\n");
  assert.equal(readFileSync(path, "utf8"), "Recovered\n");
  writeFileSync(path, Buffer.alloc(SERVICE_LOG_BYTES));
  rmSync(`${path}.1`);
  mkdirSync(`${path}.1`);
  log = serviceLog(path);
  assert.doesNotThrow(() => log("rotation blocked"));
  rmSync(`${path}.1`, { recursive: true });
  log("Rotation recovered\n");
  assert.equal(readFileSync(path, "utf8"), "Rotation recovered\n");
  console.log(
    "PASS: bounded logs, oversized writes, restart continuity and migration of oversized logs.",
  );
} finally {
  rmSync(dir, { recursive: true, force: true });
}
