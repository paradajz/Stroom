import {
  mkdirSync,
  readFileSync,
  writeFileSync,
  existsSync,
  readdirSync,
  unlinkSync,
} from "node:fs";
import { join } from "node:path";
import { contracts, benchmarkProfileFields } from "./load.mjs";

const directory = process.argv[2];
if (!directory)
  throw Error("Usage: node tools/contracts/generate.mjs OUTPUT_DIRECTORY");
mkdirSync(directory, { recursive: true });
for (const [name, values] of Object.entries(contracts)) {
  const lines = Object.entries(values).map(([key, value]) => {
    if (
      !/^[A-Z][A-Z0-9_]*$/.test(key) ||
      !key.startsWith(name.toUpperCase() + "_") ||
      !(typeof value === "string" || Number.isFinite(value))
    )
      throw Error("Invalid contract: " + key);
    return "#define " + key + " " + JSON.stringify(value);
  });
  if (name === "benchmark") {
    lines.push(
      [
        "#define BENCHMARK_PROFILE_FIELDS(X)",
        ...benchmarkProfileFields.map((field) => "    X(" + field.name + ")"),
      ].join(" \\\n"),
    );
  }
  const text = `/* Generated from shared/contracts; do not edit. */\n#pragma once\n\n${lines.join("\n")}\n`;
  const file = join(directory, name + ".h");
  if (!existsSync(file) || readFileSync(file, "utf8") !== text)
    writeFileSync(file, text);
}

// Removed definitions must not leave stale generated headers available to C.
for (const file of readdirSync(directory)) {
  if (
    file.endsWith(".h") &&
    !Object.hasOwn(contracts, file.slice(0, -2)) &&
    readFileSync(join(directory, file), "utf8").startsWith(
      "/* Generated from shared/contracts; do not edit. */",
    )
  )
    unlinkSync(join(directory, file));
}
