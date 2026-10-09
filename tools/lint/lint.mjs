import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawn, execFileSync } from "node:child_process";

const root = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
);
const [appPath, testsPath, outputPath] = process.argv
  .slice(2)
  .map((p) => path.resolve(p));
if (!appPath || !testsPath || !outputPath) {
  throw new Error(
    "Usage: node tools/lint/lint.mjs APP_BUILD TEST_BUILD LINT_BUILD",
  );
}
const tidy = process.env.CLANG_TIDY || "clang-tidy";
const jobs = Number(process.env.JOBS || 2);
if (!Number.isInteger(jobs) || jobs < 1) {
  throw new Error("JOBS must be a positive integer");
}

/**
 * Decode a CMake compile command without invoking a shell.
 * @param {string} command Shell-quoted command.
 * @returns {string[]} Executable and arguments.
 */
function words(command) {
  const result = [];
  let value = "",
    quote = null,
    started = false;
  for (let i = 0; i < command.length; ++i) {
    const ch = command[i];
    if (ch === "\\" && quote !== "'") {
      if (++i === command.length) {
        throw new Error("Incomplete escape in compile command");
      }
      value += command[i];
      started = true;
    } else if (quote) {
      if (ch === quote) {
        quote = null;
      } else {
        value += ch;
      }
    } else if (ch === "'" || ch === '"') {
      quote = ch;
      started = true;
    } else if (/\s/.test(ch)) {
      if (started) {
        result.push(value);
      }
      value = "";
      started = false;
    } else {
      value += ch;
      started = true;
    }
  }
  if (quote) {
    throw new Error("Unterminated quote in compile command");
  }
  if (started) {
    result.push(value);
  }
  return result;
}

// Make invokes this runner separately for each app configuration. Host tests
// retain their own CMake settings; deduplication is limited to each invocation.
const database = new Map();
const newlibPaths = new Map();
execFileSync(tidy, ["--verify-config"], { cwd: root, stdio: "pipe" });
for (const [dir, ps2] of [
  [appPath, true],
  [testsPath, false],
]) {
  const entries = JSON.parse(
    fs.readFileSync(path.join(dir, "compile_commands.json"), "utf8"),
  );
  for (const entry of entries) {
    const file = path.resolve(entry.directory, entry.file);
    const relative = path.relative(root, file);
    if (!/^(src|tools|tests)\/.*\.c$/.test(relative) || database.has(file)) {
      continue;
    }
    const args = entry.arguments || words(entry.command);
    if (ps2) {
      // Clang has no R5900 target. MIPS N32 retains 32-bit pointers/long and
      // 128-bit integer support needed to parse the EE SDK's public types.
      let newlib = newlibPaths.get(args[0]);
      if (!newlib) {
        const libc = execFileSync(args[0], ["-print-file-name=libc.a"], {
          encoding: "utf8",
        }).trim();
        newlib = path.resolve(path.dirname(libc), "../include");
        if (
          !path.isAbsolute(libc) ||
          !fs.existsSync(path.join(newlib, "stdio.h"))
        ) {
          throw new Error(`Cannot locate PS2 newlib headers for ${args[0]}`);
        }
        newlibPaths.set(args[0], newlib);
      }
      args.push("--target=mips64el-none-elf", "-mabi=n32", "-isystem", newlib);
    }
    database.set(file, { directory: entry.directory, file, arguments: args });
  }
}
if (!database.size) {
  throw new Error("No project C translation units found");
}
fs.mkdirSync(outputPath, { recursive: true });
fs.writeFileSync(
  path.join(outputPath, "compile_commands.json"),
  JSON.stringify([...database.values()], null, 2) + "\n",
);
const files = [...database.keys()].sort();
let cursor = 0,
  failed = 0;
/**
 * Check queued files and collect diagnostics and failures.
 * @returns {Promise<void>} Resolves when the shared queue is exhausted.
 */
async function worker() {
  while (cursor < files.length) {
    const file = files[cursor++];
    const result = await new Promise((resolve, reject) => {
      const child = spawn(tidy, [file, "-p", outputPath, "--quiet"], {
        cwd: root,
      });
      let output = "";
      child.stdout.on("data", (chunk) => {
        output += chunk;
      });
      child.stderr.on("data", (chunk) => {
        output += chunk;
      });
      child.on("error", reject);
      child.on("close", (code) => resolve({ code, output }));
    });
    if (result.code !== 0) {
      ++failed;
    }
    // SDK/system warnings are suppressed by clang-tidy; omit only its count-only
    // banner on success. Never hide diagnostics or parser failures.
    const output = result.output
      .replace(/^\d+ warnings? generated\.\n/gm, "")
      .trim();
    if (output || result.code !== 0) {
      console.log(`\n${path.relative(root, file)}\n${output}`);
    }
  }
}
console.log(`Linting ${files.length} C translation units with ${jobs} workers`);
await Promise.all(Array.from({ length: Math.min(jobs, files.length) }, worker));
console.log(
  `clang-tidy: ${files.length - failed}/${files.length} translation units passed`,
);
process.exitCode = failed ? 1 : 0;
