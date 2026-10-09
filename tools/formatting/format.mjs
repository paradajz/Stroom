import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { updateReadmeToc } from "./readme-toc.mjs";

const root = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
);
const buildPaths = process.argv
  .slice(2)
  .map((directory) => path.resolve(root, directory));
if (!buildPaths.length) {
  throw new Error("Pass the configured build directories from make format");
}
const clang = process.env.CLANG_FORMAT || "clang-format";
const prettier = process.env.PRETTIER || "prettier";
const skippedDirectories = new Set([
  "build",
  "third_party",
  "node_modules",
  ".git",
]);

// Validate both formatters before changing any files.
for (const tool of [clang, prettier]) {
  execFileSync(tool, ["--version"], { cwd: root, stdio: "pipe" });
}

/**
 * Identify configured build paths and their descendants without glob matching.
 * @param {string} filename Absolute path to check.
 * @returns {boolean} Whether the path belongs to a configured build directory.
 */
function isBuildPath(filename) {
  return buildPaths.some((directory) => {
    const relative = path.relative(directory, filename);
    return (
      relative === "" ||
      (!path.isAbsolute(relative) &&
        relative !== ".." &&
        !relative.startsWith(`..${path.sep}`))
    );
  });
}

/**
 * Collect regular files while pruning build output and dependency directories.
 * @param {string} directory Absolute directory to traverse.
 * @returns {string[]} Absolute paths eligible for formatting.
 */
function collectFiles(directory) {
  if (isBuildPath(directory)) {
    return [];
  }
  const files = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const filename = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      if (!skippedDirectories.has(entry.name)) {
        files.push(...collectFiles(filename));
      }
    } else if (entry.isFile() && !isBuildPath(filename)) {
      files.push(filename);
    }
  }
  return files;
}

/**
 * Format bounded batches without shell expansion or command-line size overflow.
 * @param {string} tool Executable path or name.
 * @param {string[]} args Fixed command arguments.
 * @param {string[]} files Absolute input paths.
 */
function runBatches(tool, args, files) {
  const batchSize = 100;
  for (let start = 0; start < files.length; start += batchSize) {
    execFileSync(tool, [...args, ...files.slice(start, start + batchSize)], {
      cwd: root,
      stdio: "inherit",
    });
  }
}

const files = collectFiles(root);
for (const filename of files) {
  if (/^readme\.md$/i.test(path.basename(filename))) {
    updateReadmeToc(filename);
  }
}
const cFiles = files.filter((filename) => /\.(c|h|cpp|hpp)$/.test(filename));
runBatches(clang, ["--style=file", "-i"], cFiles);
runBatches(
  process.execPath,
  [path.join(root, "tools/formatting/format-c-spacing.mjs")],
  cFiles,
);
// Prettier still applies .prettierignore and skips unsupported file types.
runBatches(prettier, ["--write", "--ignore-unknown"], files);
