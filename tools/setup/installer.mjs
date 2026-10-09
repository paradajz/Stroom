import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { configureInstaller } from "./installer-profile.mjs";

const root = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
);
const releases = [
  {
    repo: "israpps/KELFBinder",
    tag: "v1.1.1",
    name: "KELFBinder-v1.1.1.7z",
    asset: "KELFBinder-v1.1.1.7z",
    sha256: "831712bf84ffdf2a5815f7b5147bb69ce85d6daf260c86fcbfbd482b2efc7cff",
    directory: "KELFBinder",
  },
  {
    repo: "israpps/PlayStation2-Basic-BootLoader",
    tag: "kelf",
    name: "PS2BBL_KELF-1a2af22.7z",
    asset: "PS2BBL_KELF.7z",
    sha256: "ff8c7e86537f0c38e42bef5cc27e98586468812fb619b277cf39993a82f5eb3f",
    directory: "PS2BBL_KELF-[28-06-2025]-[1a2af22]",
  },
];

async function archive(release) {
  const directory = path.join(root, "build/downloads/signed-boot");
  fs.mkdirSync(directory, { recursive: true });
  const cached = path.join(directory, release.name);
  let data;
  if (fs.existsSync(cached)) {
    data = fs.readFileSync(cached);
  } else {
    console.log(`Downloading ${release.name}`);
    const response = await fetch(
      `https://github.com/${release.repo}/releases/download/${release.tag}/${release.asset}`,
    );
    if (!response.ok)
      throw new Error(`Download failed: HTTP ${response.status}`);
    data = Buffer.from(await response.arrayBuffer());
  }
  if (createHash("sha256").update(data).digest("hex") !== release.sha256) {
    throw new Error(`Checksum mismatch: ${cached}. Remove it and retry.`);
  }
  if (!fs.existsSync(cached)) fs.writeFileSync(cached, data);
  return cached;
}

async function prepare() {
  if (!process.argv[2]) throw new Error("Run make installer");
  const extractor = ["7z", "7zz"].find(
    (command) => spawnSync(command, ["i"], { stdio: "ignore" }).status === 0,
  );
  if (!extractor) {
    throw new Error(
      "7-Zip is required. Rebuild the development container, or install 7zip.",
    );
  }
  const output = path.resolve(root, process.argv[2]);
  fs.mkdirSync(path.dirname(output), { recursive: true });
  const temporary = fs.mkdtempSync(
    path.join(path.dirname(output), ".installer-"),
  );
  try {
    const sources = [];
    for (const [index, release] of releases.entries()) {
      const packed = await archive(release);
      const extracted = path.join(temporary, `source-${index}`);
      const result = spawnSync(
        extractor,
        ["x", "-y", `-o${extracted}`, packed],
        {
          encoding: "utf8",
        },
      );
      if (result.status !== 0)
        throw new Error(result.stderr || "Archive extraction failed");
      sources.push(path.join(extracted, release.directory));
    }
    const staging = path.join(temporary, "package");
    configureInstaller(
      staging,
      sources[0],
      sources[1],
      path.join(root, "tools/setup/PS2BBL.INI"),
    );
    fs.copyFileSync(
      path.join(root, "setup-manual.md"),
      path.join(staging, "setup-manual.md"),
    );
    fs.writeFileSync(
      path.join(staging, "UPSTREAM.txt"),
      [
        "KELFBinder v1.1.1 (GPLv3): https://github.com/israpps/KELFBinder/tree/v1.1.1",
        "PS2BBL 1a2af22 (GPLv3): https://github.com/israpps/PlayStation2-Basic-BootLoader/tree/1a2af22b3e51621b197489cb0f53e15aee71637d",
        ...releases.map(
          (release) => `${release.name}: SHA256 ${release.sha256}`,
        ),
        "",
      ].join("\n"),
    );
    fs.rmSync(output, { recursive: true, force: true });
    fs.renameSync(staging, output);
    console.log(`One-time MagicGate installer: ${output}`);
    console.log(
      "Copy this folder to USB, then copy the entire installer folder to mc0:/installer/ using wLaunchELF.",
    );
    console.log(
      "Launch mc0:/installer/KELFBinder.elf from the memory card with its subdirectories in place.",
    );
    console.log(
      "Install Manage System Updates -> Advanced Install -> Cross Model, with extra files enabled.",
    );
    console.log(
      "Copy BOOT from make prod or make dev to mc0:/ separately. See setup-manual.md.",
    );
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

try {
  await prepare();
} catch (error) {
  console.error(`Installer preparation failed: ${error.message}`);
  process.exitCode = 1;
}
