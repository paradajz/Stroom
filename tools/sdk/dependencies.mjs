import { createHash } from "node:crypto";
import { readFile, writeFile, readdir, chmod, access } from "node:fs/promises";
import { join } from "node:path";

const [action, root] = process.argv.slice(2);
if (!["freeze", "verify"].includes(action) || !root)
  throw Error(
    "Usage: node tools/sdk/dependencies.mjs freeze|verify SDK_SOURCE_DIR",
  );
const manifestPath = join(root, ".stroom-frozen-dependencies.json");
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
async function sourceFiles(directory) {
  const result = [];
  for (const entry of await readdir(join(root, directory), {
    withFileTypes: true,
  })) {
    const file = join(directory, entry.name);
    if (entry.isDirectory()) result.push(...(await sourceFiles(file)));
    else if (entry.isFile()) result.push(file);
  }
  return result.sort();
}

if (action === "freeze") {
  const exists = await access(manifestPath).then(
    () => true,
    () => false,
  );
  if (exists)
    throw Error(
      "SDK dependency sources are already frozen; refusing to replace their baseline.",
    );
  const files = await sourceFiles("common/external_deps/lwip/src");
  if (!files.length)
    throw Error("Fetch lwIP before freezing SDK dependencies.");
  // Nested common/external_deps builds invoke the script directly, bypassing
  // make -o download_dependencies. Replace it only inside this isolated copy.
  await writeFile(
    join(root, "download_dependencies.sh"),
    `#!/bin/sh
set -eu
sdk_source_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -d "$sdk_source_root/common/external_deps/lwip"
test -d "$sdk_source_root/common/external_deps/fatfs"
# Dependencies were fetched and patched before this build; never reset them here.
`,
  );
  await chmod(join(root, "download_dependencies.sh"), 0o755);
  files.push("download_dependencies.sh");
  const hashes = {};
  for (const file of files)
    hashes[file] = hash(await readFile(join(root, file)));
  await writeFile(manifestPath, JSON.stringify(hashes));
  console.log(
    `Frozen ${files.length - 1} patched lwIP source files against nested dependency resets.`,
  );
} else {
  const hashes = JSON.parse(await readFile(manifestPath, "utf8"));
  for (const [file, expected] of Object.entries(hashes)) {
    if (hash(await readFile(join(root, file))) !== expected)
      throw Error(`SDK dependency changed during the build: ${file}`);
  }
  console.log("Patched lwIP sources survived the complete SDK build.");
}
