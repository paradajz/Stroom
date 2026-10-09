import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { test } from "node:test";

const root = process.argv[2];
const { configureProfile } = await import(
  pathToFileURL(path.join(root, "tools/setup/profile.mjs"))
);
const elf = Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 2, 3, 4]);

function fixture(t) {
  const directory = fs.mkdtempSync(path.join(tmpdir(), "stroom-setup-test-"));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const card = path.join(directory, "card");
  fs.mkdirSync(card);
  const options = {
    binary: path.join(directory, "application.elf"),
    stroomConfig: path.join(directory, "STROOM.DAT"),
    defaultStroomConfig: path.join(directory, "STROOM.DAT.example"),
    ps2linkConfig: path.join(directory, "IPCONFIG.DAT"),
  };
  fs.writeFileSync(options.binary, elf);
  fs.writeFileSync(options.defaultStroomConfig, "cd_autoplay=off\n");
  fs.writeFileSync(
    options.ps2linkConfig,
    "192.168.0.10 255.0.0.0 192.168.0.1\n",
  );
  return { card, directory, options };
}

function checkBoot(card, config) {
  assert.deepEqual(fs.readdirSync(card), ["BOOT"]);
  assert.deepEqual(
    fs.readdirSync(path.join(card, "BOOT")).sort(),
    ["BOOT.ELF", config].sort(),
  );
  assert.deepEqual(fs.readFileSync(path.join(card, "BOOT/BOOT.ELF")), elf);
}

test("production contains only the direct DEV1 ELF and local settings", (t) => {
  const { card, options } = fixture(t);
  const settings = "cd_autoplay=off\nmute=on\n";
  fs.writeFileSync(options.stroomConfig, settings);
  assert.equal(
    configureProfile(card, { ...options, mode: "prod" }),
    "mc0:/BOOT/BOOT.ELF",
  );
  checkBoot(card, "STROOM.DAT");
  assert.equal(
    fs.readFileSync(path.join(card, "BOOT/STROOM.DAT"), "utf8"),
    settings,
  );
});

test("production supplies example settings when no local config exists", (t) => {
  const { card, options } = fixture(t);
  configureProfile(card, { ...options, mode: "prod" });
  checkBoot(card, "STROOM.DAT");
  assert.deepEqual(
    fs.readFileSync(path.join(card, "BOOT/STROOM.DAT")),
    fs.readFileSync(options.defaultStroomConfig),
  );
});

test("development contains only the direct DEV1 ELF and local static network settings", (t) => {
  const { card, options } = fixture(t);
  fs.writeFileSync(
    options.stroomConfig,
    "# ip=ignored\nip=192.168.9.22 # console\nnetmask=255.255.255.0\ngateway=192.168.9.1\ncd_autoplay=off\n",
  );
  assert.equal(
    configureProfile(card, { ...options, mode: "dev" }),
    "mc0:/BOOT/BOOT.ELF",
  );
  checkBoot(card, "IPCONFIG.DAT");
  assert.equal(
    fs.readFileSync(path.join(card, "BOOT/IPCONFIG.DAT"), "utf8"),
    "192.168.9.22 255.255.255.0 192.168.9.1\r\n",
  );
});

for (const local of [undefined, "cd_autoplay=off\n# ip=192.168.9.22\n"]) {
  test(`development uses upstream network settings with ${local ? "no static settings" : "no local file"}`, (t) => {
    const { card, options } = fixture(t);
    if (local) fs.writeFileSync(options.stroomConfig, local);
    configureProfile(card, { ...options, mode: "dev" });
    checkBoot(card, "IPCONFIG.DAT");
    assert.deepEqual(
      fs.readFileSync(path.join(card, "BOOT/IPCONFIG.DAT")),
      fs.readFileSync(options.ps2linkConfig),
    );
  });
}

test("incomplete or invalid static settings fail instead of selecting another network", (t) => {
  for (const settings of [
    "ip=192.168.9.22\n",
    "ip=invalid\nnetmask=255.255.255.0\ngateway=192.168.9.1\n",
  ]) {
    const { card, options } = fixture(t);
    fs.writeFileSync(options.stroomConfig, settings);
    assert.throws(
      () => configureProfile(card, { ...options, mode: "dev" }),
      /valid ip, netmask and gateway/,
    );
  }
});

test("missing or non-ELF application inputs are rejected", (t) => {
  const { card, options } = fixture(t);
  fs.writeFileSync(options.binary, "not an ELF");
  assert.throws(
    () => configureProfile(card, { ...options, mode: "prod" }),
    /Not an ELF/,
  );
  fs.rmSync(options.binary);
  assert.throws(
    () => configureProfile(card, { ...options, mode: "dev" }),
    /ENOENT/,
  );
});

test("unknown modes are rejected", (t) => {
  const { card } = fixture(t);
  for (const mode of [undefined, "setup", "other"]) {
    assert.throws(
      () => configureProfile(card, { mode }),
      /Unknown package mode/,
    );
  }
});

test("repackaging removes obsolete bootloader files and failed packaging preserves the previous package", (t) => {
  const { directory, options } = fixture(t);
  const output = path.join(directory, "memcard");
  fs.mkdirSync(path.join(output, "SYS-CONF"), { recursive: true });
  fs.writeFileSync(
    path.join(output, "SYS-CONF/PS2BBL.INI"),
    "old configuration",
  );
  const run = () =>
    spawnSync(
      process.execPath,
      [path.join(root, "tools/setup/prepare.mjs"), "prod", options.binary],
      { cwd: root, encoding: "utf8" },
    );
  const result = run();
  assert.equal(result.status, 0, result.stderr);
  checkBoot(output, "STROOM.DAT");
  fs.writeFileSync(options.binary, "broken ELF");
  const failed = run();
  assert.notEqual(failed.status, 0);
  assert.match(failed.stderr, /Not an ELF/);
  checkBoot(output, "STROOM.DAT");
  assert(!fs.readdirSync(directory).some((name) => name.startsWith(".prod-")));
});
