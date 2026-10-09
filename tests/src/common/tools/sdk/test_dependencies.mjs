import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtemp, mkdir, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const tool = fileURLToPath(
  new URL("../../../../../tools/sdk/dependencies.mjs", import.meta.url),
);
const root = await mkdtemp(join(tmpdir(), "stroom-sdk-dependencies-"));
try {
  await mkdir(join(root, "common/external_deps/lwip/src"), { recursive: true });
  await mkdir(join(root, "common/external_deps/fatfs"));
  const source = join(root, "common/external_deps/lwip/src/api.c");
  await writeFile(source, "patched\n");
  await writeFile(
    join(root, "download_dependencies.sh"),
    '#!/bin/sh\nprintf "upstream\\n" > common/external_deps/lwip/src/api.c\n',
    { mode: 0o755 },
  );
  await writeFile(
    join(root, "Makefile"),
    "all:\n\t$(MAKE) -C common/external_deps\n.PHONY: all download_dependencies\ndownload_dependencies:\n\t./download_dependencies.sh\n",
  );
  await writeFile(
    join(root, "common/external_deps/Makefile"),
    "all:\n\tcd ../.. && ./download_dependencies.sh\n",
  );
  const make = () =>
    spawnSync("make", ["-o", "download_dependencies", "all"], {
      cwd: root,
      encoding: "utf8",
    });
  assert.equal(make().status, 0);
  assert.equal(
    await readFile(source, "utf8"),
    "upstream\n",
    "The original nested-build reset must reproduce",
  );
  await writeFile(source, "patched\n");
  const invoke = (action) =>
    spawnSync(process.execPath, [tool, action, root], { encoding: "utf8" });
  const frozen = invoke("freeze");
  assert.equal(frozen.status, 0, frozen.stderr);
  assert.equal(make().status, 0);
  assert.equal(await readFile(source, "utf8"), "patched\n");
  assert.equal(invoke("verify").status, 0);
  await writeFile(source, "unexpected reset\n");
  const changed = invoke("verify");
  assert.equal(changed.status, 1);
  assert.match(changed.stderr, /dependency changed during the build/);
  assert.equal(
    invoke("freeze").status,
    1,
    "Do not silently re-baseline changed sources",
  );
} finally {
  await rm(root, { recursive: true, force: true });
}
console.log("Nested SDK dependency resets reproduced and prevented");
