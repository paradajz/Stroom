import fs from "node:fs";
import path from "node:path";

/** Keep the installer runtime, but install only the signed loader and our config. */
export function configureInstaller(destination, binder, loader, config) {
  const copy = (source, target) => {
    const output = path.join(destination, target);
    fs.mkdirSync(path.dirname(output), { recursive: true });
    fs.copyFileSync(source, output);
  };

  for (const file of ["KELFBinder.elf", "LICENSE.TXT"]) {
    copy(path.join(binder, file), file);
  }
  for (const directory of ["common", "lang", "INSTALL/CORE", "INSTALL/KELF"]) {
    fs.cpSync(path.join(binder, directory), path.join(destination, directory), {
      recursive: true,
    });
  }
  for (const file of ["KELFBinder.lua", "EXTINST.lua"]) {
    copy(path.join(binder, "INSTALL", file), `INSTALL/${file}`);
  }
  // These icons belong to the signed system update, not to bundled applications.
  for (const file of [
    "EUR.sys",
    "USA.sys",
    "JPN.sys",
    "CHN.sys",
    "PS2BBL.icn",
  ]) {
    copy(path.join(binder, "INSTALL/ASSETS", file), `INSTALL/ASSETS/${file}`);
  }
  copy(path.join(loader, "SYSTEM.XLF"), "INSTALL/KELF/SYSTEM.XLF");
  // Test the actual loader that will be bound to the card.
  copy(path.join(loader, "SYSTEM.XLF"), "INSTALL/KELF/BENCHMARK.XLF");
  // Both projects use GPLv3; the KELF archive omits its license text.
  copy(path.join(binder, "LICENSE.TXT"), "PS2BBL-LICENSE.TXT");
  copy(config, "INSTALL/ASSETS/SYS-CONF/PS2BBL.INI");

  const table = path.join(destination, "INSTALL/EXTINST.lua");
  const original = fs.readFileSync(table, "utf8");
  const mappings = /^Update_InstTable\([^\r\n]+\)[ \t]*$/gm;
  if ([...original.matchAll(mappings)].length !== 7) {
    throw new Error("Unexpected KELFBinder installation table layout");
  }
  let mappingIndex = 0;
  const updated = original.replace(mappings, () =>
    mappingIndex++ === 0
      ? 'Update_InstTable("INSTALL/ASSETS/SYS-CONF", "SYS-CONF", MC_INST_TABLE.source, MC_INST_TABLE.target, MC_INST_TABLE.dirs)'
      : "",
  );
  fs.writeFileSync(table, updated);
}
