import fs from "node:fs";
import path from "node:path";
import { isIPv4 } from "node:net";
import { spawn } from "node:child_process";
import { streamAudio } from "./benchmark_audio.mjs";
import { BenchmarkResults } from "./benchmark_results.mjs";
import { archiveBenchmark } from "./benchmark_reports.mjs";

const [address, buildArg] = process.argv.slice(2);
if (!isIPv4(address ?? "") || !buildArg) {
  console.error("Usage: make benchmark PS2_IP=<console IPv4 address>");
  process.exit(1);
}
const build = path.resolve(buildArg);
const report = JSON.parse(
  fs.readFileSync(path.join(build, "generated/preset-report.json"), "utf8"),
);
const results = new BenchmarkResults(report.entries);
const directory = path.join(
  build,
  "benchmark",
  new Date().toISOString().replaceAll(/[:.]/g, "-"),
);
fs.mkdirSync(directory, { recursive: true });
const transcript = fs.createWriteStream(path.join(directory, "console.log"));
const eventFile = path.join(build, "benchmark-events.jsonl");
fs.writeFileSync(eventFile, "");
let audio;
const client = spawn(
  process.env.PS2CLIENT || "ps2client",
  ["-h", address, "execee", "host:benchmark.elf"],
  { cwd: build, stdio: ["ignore", "pipe", "pipe"] },
);
console.log(`Benchmark reports: ${directory}`);
let partial = "";
let finished = false;
let lastEvent = Date.now();

function save() {
  const filename = path.join(directory, "results.json");
  fs.writeFileSync(
    filename + ".tmp",
    JSON.stringify(results.state, null, 2) + "\n",
  );
  fs.renameSync(filename + ".tmp", filename);
  fs.writeFileSync(path.join(directory, "results.csv"), results.csv());
}

function finish(error) {
  if (finished) return;
  finished = true;
  clearInterval(watchdog);
  clearInterval(poller);
  audio?.close();
  if (error) {
    if (
      results.state.status === "running" ||
      results.state.status === "waiting"
    )
      results.state.status = "interrupted";
    results.state.error = error;
    process.exitCode = 1;
    console.error(error);
  }
  save();
  transcript.end();
  client.kill("SIGTERM");
}

function consume(chunk) {
  if (finished) return;
  transcript.write(chunk);
  partial += chunk.toString();
  let newline;
  while ((newline = partial.indexOf("\n")) >= 0) {
    const line = partial.slice(0, newline);
    partial = partial.slice(newline + 1);
    if (!line.trim() || finished) continue;
    try {
      const event = JSON.parse(line);
      results.consume(event);
      lastEvent = Date.now();
      save();
      if (event.event === "begin") {
        audio?.resetPhase();
        console.log(
          `[${event.index + 1}/${results.entries.length}] ${path.basename(results.entries[event.index].file)}`,
        );
      } else if (event.event === "result") {
        const result = results.state.results.find(
          (item) => item.index === event.index,
        );
        console.log(
          `  ${result.classification}: ${event.steady_fps.toFixed(2)} fps; work ${event.work_avg_us.toFixed(1)} us average, ${event.work_p95_us} us p95`,
        );
      } else if (event.event === "complete") {
        const archive = archiveBenchmark(
          report,
          results.state,
          path.basename(directory) + ".json",
        );
        if (archive) console.log(`Benchmark archived: ${archive}`);
        console.log(
          `BENCHMARK COMPLETE: ${results.state.results.length} presets`,
        );
        finish();
      }
    } catch (error) {
      finish(error.message);
    }
  }
}
let offset = 0;
const poller = setInterval(() => {
  if (finished) return;
  try {
    const bytes = fs.readFileSync(eventFile);
    if (bytes.length > offset) {
      consume(bytes.subarray(offset));
      offset = bytes.length;
    }
  } catch (error) {
    finish(error.message);
  }
}, 100);
client.stdout.on("data", (chunk) => {
  if (!finished) transcript.write(chunk);
});
client.stderr.on("data", (chunk) => {
  if (!finished) transcript.write(chunk);
  process.stderr.write(chunk);
});
client.on("error", (error) => finish(error.message));
client.on("exit", (code) => {
  if (!finished) finish(`PS2Link client exited before completion (${code})`);
});
const watchdog = setInterval(() => {
  if (Date.now() - lastEvent > 120000)
    finish("No benchmark progress for two minutes; partial results retained");
}, 1000);
process.on("SIGINT", () =>
  finish("Benchmark collection interrupted; partial results retained"),
);
process.on("SIGTERM", () =>
  finish("Benchmark collection interrupted; partial results retained"),
);
save();

audio = streamAudio(address, (message) => finish(message));
