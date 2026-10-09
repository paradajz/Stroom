import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { configureProfile } from "./profile.mjs";

const root = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
);
const [mode, binary, ps2linkSource = "third_party/ps2link"] =
  process.argv.slice(2);

function prepare() {
  if (!["prod", "dev"].includes(mode)) {
    throw new Error(
      `Unknown package mode: ${mode}. Use make prod or make dev.`,
    );
  }
  if (!binary) {
    throw new Error(`Missing built executable. Run make ${mode}.`);
  }
  const executable = path.resolve(root, binary);
  const output = path.join(path.dirname(executable), "memcard");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  const staging = fs.mkdtempSync(path.join(path.dirname(output), `.${mode}-`));
  try {
    const target = configureProfile(staging, {
      mode,
      binary: executable,
      stroomConfig: path.join(root, "STROOM.DAT"),
      defaultStroomConfig: path.join(root, "STROOM.DAT.example"),
      ps2linkConfig: path.resolve(root, ps2linkSource, "bin/IPCONFIG.DAT"),
    });

    // Validate and finish the new package before replacing the previous output.
    fs.rmSync(output, { recursive: true, force: true });
    fs.renameSync(staging, output);
    console.log(`Installation package: ${output}`);
    console.log(`DEV1 application: ${target}`);
    console.log("Copy BOOT to mc0:/, then set Matrix BOOT MODE to DEV1.");
  } finally {
    fs.rmSync(staging, { recursive: true, force: true });
  }
}

try {
  prepare();
} catch (error) {
  console.error(`Packaging failed: ${error.message}`);
  process.exitCode = 1;
}
