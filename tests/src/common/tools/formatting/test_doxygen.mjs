import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { doxygenIssues } from "../../../../../tools/formatting/check-doxygen.mjs";

const compressed =
  "/** @brief Validate and skip an unknown value with bounded nesting. @param j Cursor. @param depth Current depth. @return Nonzero on valid input. */";
assert.deepEqual(doxygenIssues(compressed), [
  { line: 1, command: "@param" },
  { line: 1, command: "@param" },
  { line: 1, command: "@return" },
]);
assert.deepEqual(
  doxygenIssues("\n/*!\r\n * @param[in] x Input. @returns A value.\r\n */"),
  [{ line: 3, command: "@returns" }],
);
assert.deepEqual(doxygenIssues(String.raw`/** Summary. \param x Input. */`), [
  { line: 1, command: "\\param" },
]);
assert.deepEqual(
  doxygenIssues(
    "/**\n * @brief Summary.\n * @param x Input.\n * @return Result.\n */",
  ),
  [],
);
assert.deepEqual(
  doxygenIssues("/** @brief Read @ref State using @c count. */"),
  [],
);
assert.deepEqual(doxygenIssues("/**< Member description. */"), []);
assert.deepEqual(
  doxygenIssues(String.raw`/** @brief Write \@param or \\return as text. */`),
  [],
);
assert.deepEqual(
  doxygenIssues(`/**
 * @code{.cpp}
 * sample("@brief An example. @param x Example.");
 * @endcode
 */`),
  [],
);
assert.deepEqual(
  doxygenIssues(`/**
 * @verbatim
 * @brief Example. @return Example.
 * @endverbatim
 */`),
  [],
);
// Documentation-shaped text in ordinary comments and all C/C++ literal forms
// must not be mistaken for actual Doxygen blocks.
assert.deepEqual(
  doxygenIssues(String.raw`
const char* text = "/** @brief Text. @param x Text. */";
auto raw = R"example(/** @brief Text. @return Text. */)example";
auto empty_delimiter = R"(/** @brief Text. @note Text. */)";
auto wide = LR"wide(/** @brief Text. @param x Text. */)wide";
// /** @brief Text. @param x Text. */
/* Ordinary /** @brief Text. @param x Text. */
`),
  [],
);
assert.deepEqual(
  doxygenIssues("/** @brief Summary.\n * @note Note. @warning Warning.\n */"),
  [{ line: 2, command: "@warning" }],
);

// Exercise the production formatting entry point in a small isolated checkout.
// An invalid comment must fail before any formatter or README update writes.
const directory = mkdtempSync(path.join(tmpdir(), "stroom-doxygen-"));
try {
  const tools = path.join(directory, "tools/formatting");
  mkdirSync(tools, { recursive: true });
  mkdirSync(path.join(directory, "src"));
  for (const name of [
    "format.mjs",
    "format-c-spacing.mjs",
    "readme-toc.mjs",
    "check-doxygen.mjs",
  ]) {
    copyFileSync(
      fileURLToPath(
        new URL(`../../../../../tools/formatting/${name}`, import.meta.url),
      ),
      path.join(tools, name),
    );
  }
  const filename = path.join(directory, "src/api.h");
  const marker = path.join(directory, "formatter-ran");
  const stub = path.join(directory, "formatter");
  writeFileSync(
    stub,
    '#!/bin/sh\nif [ "$1" = "--version" ]; then exit 0; fi\nprintf called >> "$STROOM_FORMAT_MARKER"\n',
    { mode: 0o700 },
  );
  const readme = "# Example\n\n## Section\n\nText.\n";
  writeFileSync(path.join(directory, "README.md"), readme);
  writeFileSync(filename, compressed + "\nvoid skip(void);\n");
  const run = () =>
    spawnSync(
      process.execPath,
      [path.join(tools, "format.mjs"), path.join(directory, "build")],
      {
        encoding: "utf8",
        env: {
          ...process.env,
          CLANG_FORMAT: stub,
          PRETTIER: stub,
          STROOM_FORMAT_MARKER: marker,
        },
      },
    );
  const rejected = run();
  assert.equal(rejected.status, 1, rejected.stderr);
  assert.match(
    rejected.stderr,
    /src\/api\.h:1: @param must start its own Doxygen comment line/,
  );
  assert.equal(
    readFileSync(filename, "utf8"),
    compressed + "\nvoid skip(void);\n",
  );
  assert.equal(readFileSync(path.join(directory, "README.md"), "utf8"), readme);
  assert(
    !existsSync(marker),
    "Invalid Doxygen must be flagged before formatting any files",
  );
  writeFileSync(
    filename,
    "/**\n * @brief Skip a value.\n * @param depth Current depth.\n */\nvoid skip(unsigned depth);\n",
  );
  const accepted = run();
  assert.equal(accepted.status, 0, accepted.stderr);
  assert(
    existsSync(marker),
    "Correct documentation should allow formatting to proceed",
  );
  console.log(
    "PASS: Doxygen command lines, literal/code exclusions, diagnostics and formatter preflight",
  );
} finally {
  rmSync(directory, { recursive: true, force: true });
}
