import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtemp, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { cdStream as wire } from "../../../../../tools/contracts/load.mjs";
import {
  streamTcpDiagnostics,
  streamTcpGapDetails,
} from "../../../../../tools/cd/tcp-diagnostics.mjs";

const host = "192.168.1.240",
  pc = "192.168.1.174.40000";
const data = (at, start, end, flags = "P.", peer = pc) =>
  `${at.toFixed(6)} IP ${host}.12891 > ${peer}: Flags [${flags}], seq ${start}:${end}, ack 7, win 32768, length ${end - start}`;
const ack = (at, sequence, window) =>
  `${at.toFixed(6)} IP ${pc} > ${host}.12891: Flags [.], ack ${sequence}, win ${window}, length 0`;
const a = streamTcpDiagnostics(host);
assert.deepEqual(a.observe("listening on enp2s0"), []);
assert.deepEqual(a.observe(data(1, 1000, 2000)), []);
a.observe(ack(1.01, 2000, 1000));
a.observe(ack(1.02, 2000, 1000));
a.observe(ack(1.03, 2000, 2000)); // Window update is not a duplicate ACK.
assert.deepEqual(a.observe(ack(1.04, 2000, 0)), [
  `PC advertised a zero receive window (${pc})`,
]);
assert.deepEqual(a.observe(ack(1.05, 2000, 0)), []);
assert.match(a.observe(data(2.5, 1000, 2000))[0], /1500.0 ms/);
a.observe(data(2.51, 2000, 3000));
assert.deepEqual(a.totals, {
  packets: 8,
  dataPackets: 3,
  repeats: 1,
  duplicateAcks: 2,
  zeroWindows: 2,
  maxGapMs: 1500,
  startupSessions: 0,
  maxStartupMs: 0,
});
assert.equal(a.gaps[0].previousEnd, 2000);
assert.equal(a.gaps[0].nextSequence, 1000);
assert.equal(a.gaps[0].pcAck, 2000);
assert.equal(a.gaps[0].window, "0");
assert.equal(a.gaps[0].ackAgeMs, 1450);
assert.match(streamTcpGapDetails([], a), /next data sequence: 1000/);
// Independent connections and reconnects must not inflate arrival gaps/repeats.
a.observe(data(20, 100, 200, "P.", "192.168.1.174.40001"));
a.observe(
  `21.000000 IP ${host}.12891 > ${pc}: Flags [F.], seq 3000, ack 7, win 32768, length 0`,
);
assert.deepEqual(a.observe(data(30, 100, 200)), []);
assert.equal(a.totals.maxGapMs, 1500);
const wrap = streamTcpDiagnostics(host);
wrap.observe(
  "1.000000 IP 192.168.1.240.12891 > 192.168.1.174.40000: Flags [P.], seq 4294967200:4, ack 7, win 32768, length 100",
);
wrap.observe(data(1.01, 4, 104));
assert.equal(wrap.totals.repeats, 0);
wrap.observe(data(1.02, 4, 104));
assert.equal(wrap.totals.repeats, 1);

const formatBytes = wire.CD_STREAM_HEADER_BYTES + wire.CD_STREAM_FORMAT_BYTES;
const syn = (at, sequence, peer = pc) =>
  `${at.toFixed(6)} IP ${host}.12891 > ${peer}: Flags [S.], seq ${sequence}, ack 7, win 32768, length 0`;
const startup = streamTcpDiagnostics(host);
startup.observe(syn(0.9, 6523));
startup.observe(data(1, 6524, 6524 + formatBytes));
assert.match(
  startup.observe(data(5.1873, 6548, 8008))[0],
  /startup wait 4187.3 ms/,
);
startup.observe(data(5.2197, 8008, 9120));
assert.equal(startup.totals.startupSessions, 1);
assert.equal(startup.totals.maxStartupMs.toFixed(1), "4187.3");
assert.equal(startup.totals.maxGapMs.toFixed(1), "32.4");
assert.equal(startup.gaps.length, 0);
assert.match(
  streamTcpGapDetails([], startup),
  /4187.3 ms \(excluded from playback gaps\)/,
);
// Real gaps after the first audio packet remain visible.
assert.match(
  startup.observe(data(6.5, 9120, 10580))[0],
  /data arrival gap 1280.3 ms/,
);

// Fragmentation, FORMAT retransmission and sequence wrap retain startup identity.
const fragmented = streamTcpDiagnostics(host);
const start = 0xfffffff8;
fragmented.observe(syn(0.9, start - 1));
fragmented.observe(data(1, start, start + 4));
fragmented.observe(data(2, start, start + 4));
fragmented.observe(
  `3.000000 IP ${host}.12891 > ${pc}: Flags [P.], seq ${start + 4}:16, ack 7, win 32768, length ${formatBytes - 4}`,
);
assert.match(
  fragmented.observe(data(5, 16, 1476))[0],
  /startup wait 4000.0 ms/,
);
assert.equal(fragmented.totals.maxGapMs, 0);
assert.equal(fragmented.totals.repeats, 1);
fragmented.observe(syn(10, 99));
fragmented.observe(data(10.1, 100, 100 + formatBytes));
fragmented.observe(data(10.2, 100 + formatBytes, 200));
assert.equal(fragmented.totals.startupSessions, 2);
assert.equal(fragmented.totals.maxGapMs, 0);

// A coalesced FORMAT/audio packet begins playback immediately.
const coalesced = streamTcpDiagnostics(host);
coalesced.observe(syn(0.9, 99));
coalesced.observe(data(1, 100, 1100));
coalesced.observe(data(1.5, 1100, 2100));
assert.equal(coalesced.totals.maxStartupMs, 0);
assert.equal(coalesced.totals.startupSessions, 1);
assert.equal(coalesced.totals.maxGapMs, 500);

// A small first packet in a midstream capture must not hide a genuine stall.
const midstream = streamTcpDiagnostics(host);
midstream.observe(data(1, 100, 100 + formatBytes));
assert.match(
  midstream.observe(data(2.5, 100 + formatBytes, 1100))[0],
  /1500.0 ms/,
);
assert.equal(midstream.totals.startupSessions, 0);
assert.match(streamTcpGapDetails([], midstream), /Startup wait unavailable/);

const directory = await mkdtemp(join(tmpdir(), "stroom-test-tcp-"));
let reportDirectory;
try {
  const scripts = {
    ip: `#!/bin/sh\nprintf '%s\\n' '[{"dev":"test-lan","prefsrc":"192.168.1.174"}]'\n`,
    sudo: '#!/bin/sh\nif [ "$1" = "-v" ]; then exit 0; fi\nexec "$@"\n',
    timeout:
      '#!/bin/sh\nif [ "$1" = "--version" ]; then exit 0; fi\nshift 2\nexec "$@"\n',
    tcpdump: `#!/bin/sh\nif [ "$1" = "--version" ]; then exit 0; fi\ncat <<'PACKETS'\n${syn(0.9, 6523)}\n${data(1, 6524, 6548)}\n${data(5.1873, 6548, 7548)}\n${ack(5.2, 7548, 1000)}\n${data(6.6873, 6548, 7548)}\nPACKETS\nprintf '%s\\n' '5 packets captured' '0 packets dropped by kernel' >&2\n`,
  };
  for (const [name, script] of Object.entries(scripts))
    await writeFile(join(directory, name), script, { mode: 0o755 });
  const tool = fileURLToPath(
    new URL("../../../../../tools/cd/diagnose-stream.mjs", import.meta.url),
  );
  const run = spawnSync(process.execPath, [tool, host, "--seconds", "1"], {
    encoding: "utf8",
    env: { ...process.env, PATH: directory + ":" + process.env.PATH },
  });
  assert.equal(run.status, 0, run.stdout + run.stderr);
  assert.match(run.stdout, /1500.0 ms/);
  assert.match(run.stdout, /sequence ranges: 1/);
  assert.match(
    run.stdout,
    /Largest gap between PS2 data packets \(observed startup excluded\): 1500.0 ms/,
  );
  assert.match(run.stdout, /longest FORMAT-to-audio wait: 4187.3 ms/);
  reportDirectory = run.stdout.match(/TCP header log: (.+)\/packets.txt/)[1];
  assert.match(
    await readFile(join(reportDirectory, "report.txt"), "utf8"),
    /0 packets dropped by kernel/,
  );
  assert.match(
    await readFile(join(reportDirectory, "packets.txt"), "utf8"),
    /seq 6548:7548/,
  );
  // Saved-capture analysis must not invoke capture, route lookup or sudo.
  const read = spawnSync(
    process.execPath,
    [tool, host, "--read", join(reportDirectory, "packets.txt")],
    {
      encoding: "utf8",
      env: { ...process.env, PATH: "/nonexistent" },
    },
  );
  assert.equal(read.status, 0, read.stdout + read.stderr);
  assert.match(read.stdout, /Gap 1500.0 ms/);
  assert.match(
    read.stdout,
    /Highest PS2 sequence end before gap: 7548; next data sequence: 6548/,
  );
  assert.match(read.stdout, /Last PC ACK before data resumed: 7548/);
  assert.match(read.stdout, /longest FORMAT-to-audio wait: 4187.3 ms/);
  assert.doesNotMatch(read.stdout, /Authorizing/);
  const invalid = spawnSync(process.execPath, [tool, host, "--seconds", "0"], {
    encoding: "utf8",
  });
  assert.equal(invalid.status, 1);
} finally {
  await rm(directory, { recursive: true, force: true });
  if (reportDirectory) {
    await rm(reportDirectory, { recursive: true, force: true });
    await rm(reportDirectory + ".tar.gz", { force: true });
  }
}
console.log("CD TCP capture diagnostics passed");
