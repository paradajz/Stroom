import assert from "node:assert/strict";
import fs from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { test } from "node:test";

const root = process.argv[2];
const { configureInstaller } = await import(
  pathToFileURL(path.join(root, "tools/setup/installer-profile.mjs"))
);

function fixture(t) {
  const directory = fs.mkdtempSync(
    path.join(tmpdir(), "stroom-installer-test-"),
  );
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const binder = path.join(directory, "upstream");
  const loader = path.join(directory, "loader");
  const destination = path.join(directory, "installer");
  const write = (file, data) => {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, data);
  };
  for (const file of [
    "KELFBinder.elf",
    "LICENSE.TXT",
    "common/font2.ttf",
    "lang/spanish.lua",
    "INSTALL/CORE/BACKDOOR.ELF",
    "INSTALL/KELF/SYSTEM.XLF",
    "INSTALL/KELF/OSDSYS.KERNEL",
    "INSTALL/KELFBinder.lua",
    "INSTALL/ASSETS/EUR.sys",
    "INSTALL/ASSETS/USA.sys",
    "INSTALL/ASSETS/JPN.sys",
    "INSTALL/ASSETS/CHN.sys",
    "INSTALL/ASSETS/PS2BBL.icn",
    "INSTALL/ASSETS/BOOT/BOOT.ELF",
    "INSTALL/ASSETS/APPS/OPNPS2LD.ELF",
    "INSTALL/ASSETS/PS2BBL/CONFIG.INI",
    "INSTALL/ASSETS/BOOT-HDD/BOOT.ELF",
  ])
    write(path.join(binder, file), `upstream ${file}`);
  const mappings = [
    "PS2BBL",
    "APPS",
    "BOOT",
    "PS2BBL-HDD",
    "APPS-HDD",
    "BOOT-HDD",
    "FSCK",
  ];
  write(
    path.join(binder, "INSTALL/EXTINST.lua"),
    [
      "-- Installation table and helper definitions are preserved",
      ...mappings.map(
        (name) =>
          `Update_InstTable("INSTALL/ASSETS/${name}", "${name}", source, target, dirs)`,
      ),
      "-- Table reporting is preserved",
    ].join("\n"),
  );
  write(path.join(loader, "SYSTEM.XLF"), "pinned signed-loader input");
  write(path.join(loader, "LICENSE.TXT"), "loader license");
  const config = path.join(root, "tools/setup/PS2BBL.INI");
  return { binder, loader, destination, config };
}

test("installer keeps its runtime but never copies bundled apps onto the card", (t) => {
  const f = fixture(t);
  configureInstaller(f.destination, f.binder, f.loader, f.config);
  assert.deepEqual(
    fs.readdirSync(path.join(f.destination, "INSTALL/ASSETS")).sort(),
    ["CHN.sys", "EUR.sys", "JPN.sys", "PS2BBL.icn", "SYS-CONF", "USA.sys"],
  );
  assert.deepEqual(
    fs.readdirSync(path.join(f.destination, "INSTALL/ASSETS/SYS-CONF")),
    ["PS2BBL.INI"],
  );
  assert.equal(
    fs.readFileSync(
      path.join(f.destination, "INSTALL/ASSETS/SYS-CONF/PS2BBL.INI"),
      "utf8",
    ),
    fs.readFileSync(f.config, "utf8"),
  );
  for (const file of ["SYSTEM.XLF", "BENCHMARK.XLF"]) {
    assert.equal(
      fs.readFileSync(path.join(f.destination, "INSTALL/KELF", file), "utf8"),
      "pinned signed-loader input",
    );
  }
  for (const file of [
    "KELFBinder.elf",
    "common/font2.ttf",
    "lang/spanish.lua",
    "INSTALL/CORE/BACKDOOR.ELF",
    "INSTALL/KELF/OSDSYS.KERNEL",
    "LICENSE.TXT",
    "PS2BBL-LICENSE.TXT",
  ]) {
    assert(fs.existsSync(path.join(f.destination, file)), file);
  }
  const table = fs.readFileSync(
    path.join(f.destination, "INSTALL/EXTINST.lua"),
    "utf8",
  );
  assert.match(table, /helper definitions are preserved/);
  assert.match(table, /Table reporting is preserved/);
  const calls = table.match(/^Update_InstTable\(.+$/gm);
  assert.equal(calls.length, 1);
  assert.equal(
    calls[0],
    'Update_InstTable("INSTALL/ASSETS/SYS-CONF", "SYS-CONF", MC_INST_TABLE.source, MC_INST_TABLE.target, MC_INST_TABLE.dirs)',
  );
  assert(
    table.indexOf(calls[0]) < table.indexOf("-- Table reporting is preserved"),
  );
});

test("unexpected upstream installation tables are rejected", (t) => {
  const f = fixture(t);
  fs.appendFileSync(
    path.join(f.binder, "INSTALL/EXTINST.lua"),
    '\nUpdate_InstTable("NEW", "NEW", source, target, dirs)',
  );
  assert.throws(
    () => configureInstaller(f.destination, f.binder, f.loader, f.config),
    /Unexpected KELFBinder installation table layout/,
  );
});

test("signed boot uses the existing slot-one app without a configured logo or key wait", () => {
  const text = fs.readFileSync(
    path.join(root, "tools/setup/PS2BBL.INI"),
    "utf8",
  );
  const settings = Object.fromEntries(
    text
      .split(/\r?\n/)
      .filter((line) => line && !line.startsWith("#"))
      .map((line) => line.split("=").map((part) => part.trim())),
  );
  assert.equal(settings.LK_AUTO_E1, "mc0:/BOOT/BOOT.ELF");
  assert.equal(settings.KEY_READ_WAIT_TIME, "0");
  assert.equal(settings.LOGO_DISPLAY, "0");
  assert.equal(settings.OSDHISTORY_READ, "0");
});
