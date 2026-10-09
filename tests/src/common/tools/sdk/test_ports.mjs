import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";

const root = process.argv[2];
const temp = fs.mkdtempSync(path.join(os.tmpdir(), "stroom-ports-"));

try {
  for (const name of ["Makefile", "build-cmakelibs.sh"])
    fs.copyFileSync(
      path.join(root, "third_party/ps2sdk-ports", name),
      path.join(temp, name),
    );
  execFileSync("patch", [
    "-d",
    temp,
    "-p1",
    "-i",
    path.join(root, "patches/ps2sdk-ports/select-libraries.patch"),
  ]);
  fs.chmodSync(path.join(temp, "build-cmakelibs.sh"), 0o755);

  const bin = path.join(temp, "bin");
  fs.mkdirSync(bin);
  const writeTool = (file, body) =>
    fs.writeFileSync(file, "#!/bin/sh\nset -eu\n" + body, { mode: 0o755 });
  writeTool(
    path.join(temp, "fetch.sh"),
    'name=${2##*/}\nname=${name%.git}\nprintf "%s\\n" "$name" >> "$FETCH_LOG"\nmkdir -p "build/$name"\n',
  );
  writeTool(
    path.join(bin, "cmake"),
    'printf "%s %s\\n" "$PWD" "$*" >> "$BUILD_LOG"\n',
  );
  writeTool(path.join(bin, "make"), ":\n");
  writeTool(
    path.join(bin, "wget"),
    'echo "Unexpected unrelated download" >&2\nexit 1\n',
  );

  const sdk = path.join(temp, "sdk");
  fs.mkdirSync(path.join(sdk, "ports/lib/pkgconfig"), { recursive: true });
  const selected = ["zlib", "libpng", "libjpeg-turbo", "gsKit"];
  const env = {
    ...process.env,
    PATH: bin + path.delimiter + process.env.PATH,
    PS2DEV: temp,
    PS2SDK: sdk,
    GSKIT: path.join(temp, "gsKit"),
    PORTS_LIBRARIES: selected.join(" "),
    FETCH_LOG: path.join(temp, "fetch.log"),
    BUILD_LOG: path.join(temp, "build.log"),
  };
  execFileSync("bash", [path.join(temp, "build-cmakelibs.sh")], {
    cwd: temp,
    env,
  });
  assert.deepEqual(
    fs.readFileSync(env.FETCH_LOG, "utf8").trim().split("\n").sort(),
    [...selected].sort(),
  );
  const builds = fs.readFileSync(env.BUILD_LOG, "utf8").trim().split("\n");
  assert.deepEqual(
    builds.map((line) => path.basename(path.dirname(line.split(" ")[0]))),
    selected,
  );
  assert.match(builds[0], /-DZLIB_BUILD_SHARED=OFF/);
  assert.match(builds[1], /-DPNG_STATIC=ON/);
  assert.match(builds[2], /-DWITH_SIMD=0/);
  assert.equal(
    fs.readlinkSync(path.join(sdk, "ports/lib/libpng16.a")),
    "libpng.a",
  );

  const plan = execFileSync(
    "make",
    ["-n", "-C", temp, "cmakelibs", "PORTS_LIBRARIES=" + selected.join(" ")],
    { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
  );
  assert.match(plan, /build-cmakelibs.sh/);
  assert.doesNotMatch(plan, /mmceman/);
  console.log(
    "PASS: ports selection filters downloads/builds and preserves upstream flags",
  );
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
