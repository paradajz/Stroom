import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { PC_BUFFER } from "../../../../../tools/cd/playback.mjs";

const [fixturePath] = process.argv.slice(2);
const directory = await mkdtemp(join(tmpdir(), "stroom-capture-runner-test-"));
const fixture = spawn(fixturePath, [], { stdio: ["pipe", "pipe", "pipe"] });
let fixtureLog = "";
fixture.stdout.on("data", (data) => {
  fixtureLog += data;
});
await new Promise((resolve, reject) => {
  const timer = setTimeout(
    () => reject(Error("Audio fixture did not start")),
    3000,
  );
  const observe = () => {
    if (fixtureLog.includes("READY")) {
      clearTimeout(timer);
      fixture.stdout.off("data", observe);
      resolve();
    }
  };
  fixture.stdout.on("data", observe);
  observe();
});
const scripts = {
  ip: '#!/bin/sh\nprintf \'%s\\n\' \'[{"dev":"test-lan","prefsrc":"127.0.0.1"}]\'\n',
  sudo: '#!/bin/sh\nif [ "$1" = "-v" ]; then exit 0; fi\nexec "$@"\n',
  timeout:
    '#!/bin/sh\nif [ "$1" = "--version" ]; then exit 0; fi\nshift 2\nexec "$@"\n',
  ffplay: "#!/bin/sh\ncat >/dev/null\n",
  tcpdump: `#!/bin/sh
if [ "$1" = "--version" ]; then exit 0; fi
printf '%s\\n' '1.000000 IP 127.0.0.1.12891 > 127.0.0.1.40000: Flags [P.], seq 1000:2000, ack 7, win 65535, length 1000'
/bin/sleep 2
printf '%s\\n' '1 packet captured' '0 packets dropped by kernel' >&2
`,
};
let reportDirectory;
try {
  for (const [name, source] of Object.entries(scripts))
    await writeFile(join(directory, name), source, { mode: 0o755 });
  const runner = fileURLToPath(
    new URL("../../../../../tools/cd/diagnose-stream.mjs", import.meta.url),
  );
  const child = spawn(
    process.execPath,
    [runner, "127.0.0.1", "--listen", "--seconds", "2"],
    {
      env: { ...process.env, PATH: directory + ":" + process.env.PATH },
      stdio: ["ignore", "pipe", "pipe"],
    },
  );
  let log = "";
  child.stdout.on("data", (data) => {
    log += data;
  });
  child.stderr.on("data", (data) => {
    log += data;
  });
  const producer = setInterval(() => {
    fixture.stdin.write("P");
  }, 13);
  const guard = setTimeout(() => child.kill("SIGKILL"), 12000);
  const [code] = await once(child, "close");
  clearTimeout(guard);
  clearInterval(producer);
  assert.equal(code, 0, log);
  assert.match(log, /Starting the audio listener/);
  assert.match(log, /Listening to 127.0.0.1:12891/);
  reportDirectory = log.match(/TCP header log: (.+)\/packets.txt/)[1];
  assert.ok(
    (await readFile(join(reportDirectory, "listener.txt"), "utf8")).includes(
      `Buffering ${PC_BUFFER.defaultMs} ms`,
    ),
  );
  const report = await readFile(join(reportDirectory, "report.txt"), "utf8");
  assert.match(report, /Captured TCP packets: 1; PS2 data packets: 1/);
  assert.match(report, /Repeated\/overlapping PS2 sequence ranges: 0/);
  assert.match(report, /PC zero-window advertisements: 0/);
  const archive = reportDirectory + ".tar.gz";
  const listing = spawnSync("tar", ["-tzf", archive], { encoding: "utf8" });
  assert.equal(listing.status, 0);
  for (const file of ["listener.txt", "report.txt", "packets.txt"])
    assert.ok(listing.stdout.includes(file), file);
} finally {
  fixture.stdin.end("Q");
  await once(fixture, "close");
  await rm(directory, { recursive: true, force: true });
  if (reportDirectory) {
    await rm(reportDirectory, { recursive: true, force: true });
    await rm(reportDirectory + ".tar.gz", { force: true });
  }
}
console.log(
  "One-command CD listener, TCP comparison counters and diagnostic bundle passed",
);
