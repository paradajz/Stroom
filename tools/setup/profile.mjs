import fs from "node:fs";
import path from "node:path";
import { isIPv4 } from "node:net";

function networkConfig(stroomConfig, fallback) {
  if (!fs.existsSync(stroomConfig)) return fs.readFileSync(fallback);

  const settings = new Map();
  for (const line of fs.readFileSync(stroomConfig, "utf8").split(/\r?\n/)) {
    const text = line.split("#")[0];
    const separator = text.indexOf("=");
    if (separator >= 0) {
      settings.set(
        text.slice(0, separator).trim(),
        text.slice(separator + 1).trim(),
      );
    }
  }
  const fields = ["ip", "netmask", "gateway"];
  if (!fields.some((field) => settings.has(field))) {
    return fs.readFileSync(fallback);
  }
  const values = fields.map((field) => settings.get(field));
  if (!values.every((value) => typeof value === "string" && isIPv4(value))) {
    throw new Error(
      "PS2Link requires valid ip, netmask and gateway values together in STROOM.DAT",
    );
  }
  return Buffer.from(`${values.join(" ")}\r\n`);
}

/** Package the selected application and settings for direct Matrix DEV1 boot. */
export function configureProfile(cardRoot, options) {
  const { mode, binary, stroomConfig, defaultStroomConfig, ps2linkConfig } =
    options;
  if (!["prod", "dev"].includes(mode)) {
    throw new Error(`Unknown package mode: ${mode}`);
  }

  const data = fs.readFileSync(binary);
  if (
    data.length < 4 ||
    !data.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46]))
  ) {
    throw new Error(`Not an ELF executable: ${binary}`);
  }

  const destination = path.join(cardRoot, "BOOT");
  fs.mkdirSync(destination);
  fs.writeFileSync(path.join(destination, "BOOT.ELF"), data);
  if (mode === "prod") {
    fs.copyFileSync(
      fs.existsSync(stroomConfig) ? stroomConfig : defaultStroomConfig,
      path.join(destination, "STROOM.DAT"),
    );
  } else {
    fs.writeFileSync(
      path.join(destination, "IPCONFIG.DAT"),
      networkConfig(stroomConfig, ps2linkConfig),
    );
  }
  return "mc0:/BOOT/BOOT.ELF";
}
