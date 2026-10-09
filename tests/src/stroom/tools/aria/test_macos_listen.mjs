import assert from "node:assert/strict";
import cp from "node:child_process";
import { EventEmitter } from "node:events";
import { syncBuiltinESMExports } from "node:module";
import { PassThrough, Writable } from "node:stream";
import { fileURLToPath } from "node:url";

const helper = new URL(
  "../../../../../tools/aria/macos-listen.mjs",
  import.meta.url,
);
const originalSpawn = cp.spawn;
const originalArgv = process.argv;
const modes = [
  { name: "default", args: [], input: ":default" },
  {
    name: "loopback",
    args: ["--input", "USB Audio", "--left", "9", "--right", "10"],
    input: ":USB Audio",
    channels: [9, 10],
  },
  {
    name: "mono",
    args: ["--input", "2", "--left", "3", "--right", "3"],
    input: ":2",
    channels: [3, 3],
  },
  {
    name: "right-only",
    args: ["--right", "10"],
    input: ":default",
    channels: [1, 10],
  },
];

for (const mode of modes) {
  const signals = new Map(
    ["SIGINT", "SIGTERM"].map((signal) => [signal, process.listeners(signal)]),
  );
  const samples = [];
  let capture;
  try {
    cp.spawn = (command, args, options) => {
      if (command === "ffmpeg") {
        assert.equal(args[args.indexOf("-i") + 1], mode.input);
        // Replace only the macOS device with a generated source. Run the real
        // FFmpeg conversion and channel selection before the helper pipes PCM.
        const source = mode.channels
          ? `aevalsrc=${Array.from({ length: 16 }, (_, i) => (i + 1) / 100).join("|")}:s=48000:d=0.01:c=hexadecagonal`
          : "aevalsrc=0.2:s=48000:d=0.01";
        const converted = [...args];
        converted[converted.indexOf("avfoundation")] = "lavfi";
        converted[converted.indexOf("-i") + 1] = source;
        capture = originalSpawn(command, converted, options);
        return capture;
      }
      assert.equal(command, process.execPath);
      assert.ok(args.includes("--listen"));
      assert.equal(args[args.indexOf("--host") + 1], "ps2.local");
      assert.equal(args[args.indexOf("--device-name") + 1], "Audio input");
      const sender = new EventEmitter();
      sender.stdin = new Writable({
        write(chunk, encoding, done) {
          samples.push(Buffer.from(chunk));
          done();
        },
      });
      sender.stderr = new PassThrough();
      let stopped = false;
      sender.kill = () => {
        if (stopped) return;
        stopped = true;
        process.emit("SIGTERM");
        queueMicrotask(() => {
          sender.emit("exit", 0);
          sender.emit("close", 0);
        });
      };
      return sender;
    };
    syncBuiltinESMExports();
    process.argv = [
      process.execPath,
      fileURLToPath(helper),
      "ffmpeg",
      "ps2.local",
      ...mode.args,
    ];
    await import(new URL(`${helper.href}?${mode.name}`));
    const pcm = Buffer.concat(samples);
    assert.equal(pcm.length, 480 * 2 * 2, "48 kHz stereo S16_LE for 10 ms");
    for (let offset = 0; offset < pcm.length; offset += 4) {
      const left = pcm.readInt16LE(offset);
      const right = pcm.readInt16LE(offset + 2);
      if (mode.channels) {
        assert.ok(Math.abs(left - (mode.channels[0] / 100) * 32768) <= 1);
        assert.ok(Math.abs(right - (mode.channels[1] / 100) * 32768) <= 1);
      } else {
        assert.ok(left > 0);
        assert.equal(left, right, "Default mono input reaches both outputs");
      }
    }
  } finally {
    capture?.kill("SIGKILL");
    cp.spawn = originalSpawn;
    syncBuiltinESMExports();
    process.argv = originalArgv;
    for (const [signal, previous] of signals)
      for (const listener of process.listeners(signal))
        if (!previous.includes(listener))
          process.removeListener(signal, listener);
  }
}

for (const args of [
  ["--left", "0"],
  ["--right", "1.5"],
  ["--left", "9007199254740993"],
  ["--input", ""],
  ["--unknown"],
]) {
  const result = cp.spawnSync(
    process.execPath,
    [fileURLToPath(helper), "ffmpeg", "ps2.local", ...args],
    { encoding: "utf8" },
  );
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Usage:/u);
}
const help = cp.spawnSync(process.execPath, [fileURLToPath(helper), "--help"], {
  encoding: "utf8",
});
assert.equal(help.status, 0);
assert.match(help.stdout, /system default input/u);
console.log(
  "PASS: default mono capture, device selection, loopback and mono channel mapping, partial channel defaults, and CLI validation.",
);
