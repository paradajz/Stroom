import {
  appendFileSync,
  closeSync,
  existsSync,
  openSync,
  readSync,
  renameSync,
  statSync,
  writeFileSync,
} from "node:fs";

export const SERVICE_LOG_BYTES = 1024 * 1024;

/** Keep one current log and one backup, including when upgrading an unbounded log. */
export function serviceLog(path) {
  const backup = `${path}.1`;
  let size = null;
  let warned = false;
  function failed(error) {
    size = null;
    if (!warned) {
      console.error(
        `Service logging unavailable: ${error.message}; streaming continues.`,
      );
      warned = true;
    }
  }
  function initialize() {
    for (const file of [path, backup]) {
      if (!existsSync(file) || statSync(file).size <= SERVICE_LOG_BYTES)
        continue;
      const tail = Buffer.alloc(SERVICE_LOG_BYTES);
      const fd = openSync(file, "r");
      let bytes;
      try {
        bytes = readSync(
          fd,
          tail,
          0,
          tail.length,
          statSync(file).size - tail.length,
        );
      } finally {
        closeSync(fd);
      }
      writeFileSync(file, tail.subarray(0, bytes), { mode: 0o600 });
    }
    size = existsSync(path) ? statSync(path).size : 0;
  }
  try {
    initialize();
  } catch (error) {
    failed(error);
  }
  return (data) => {
    try {
      if (size === null) initialize();
      const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data);
      let offset = 0;
      while (offset < bytes.length) {
        if (size === SERVICE_LOG_BYTES) {
          renameSync(path, backup);
          size = 0;
        }
        const count = Math.min(SERVICE_LOG_BYTES - size, bytes.length - offset);
        appendFileSync(path, bytes.subarray(offset, offset + count), {
          mode: 0o600,
        });
        size += count;
        offset += count;
      }
      warned = false;
    } catch (error) {
      failed(error);
    }
  };
}
