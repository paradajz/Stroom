import assert from "node:assert/strict";
import http from "node:http";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";

export const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
export const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
export async function serviceConfig(directory, settings = {}) {
  const path = directory + "/service-config";
  await writeFile(
    path,
    JSON.stringify({
      contact: "test@example.test",
      cache_directory: directory,
      ...settings,
    }),
  );
  return path;
}
export class Service {
  static async create(fixture = "service_fixture.mjs", env = {}) {
    const service = new Service();
    service.directory = await mkdtemp("/tmp/stroom-http-test-");
    service.fixture = fixture;
    service.env = env;
    service.peer = 1;
    return service;
  }
  async start(extra = []) {
    this.output = "";
    const path = await serviceConfig(this.directory, {
      contact: this.contact ?? "test@example.test",
      cache_directory: this.outputDirectory ?? this.directory,
      ...this.config,
    });
    this.child = spawn(
      process.execPath,
      [
        "--import",
        fileURLToPath(new URL(this.fixture, import.meta.url)),
        fileURLToPath(
          new URL(
            "../../../../../tools/cd/recognition/service.mjs",
            import.meta.url,
          ),
        ),
        "--config",
        path,
        ...extra,
      ],
      {
        env: { ...process.env, ...this.env },
        stdio: ["ignore", "pipe", "pipe", "ipc"],
      },
    );
    this.child.stdout.on("data", (data) => (this.output += data));
    this.child.stderr.on("data", (data) => (this.output += data));
    await this.until(() => this.output.includes("CD recognition listening"));
    return this;
  }
  async until(check) {
    const end = Date.now() + 10000;
    while (!(await check())) {
      assert.equal(this.child?.exitCode, null, this.output);
      assert.ok(Date.now() < end, this.output);
      await delay(10);
    }
  }
  async control(message = {}) {
    const reply = once(this.child, "message");
    this.child.send(message);
    return (await reply)[0];
  }
  async request(
    path,
    {
      body,
      raw,
      method = body !== undefined || raw !== undefined ? "POST" : "GET",
      ip,
      headers = {},
      pace = true,
    } = {},
  ) {
    if (pace) await delay(55);
    const bytes =
      raw !== undefined
        ? Buffer.from(raw)
        : body !== undefined
          ? Buffer.from(JSON.stringify(body))
          : null;
    return new Promise((resolve, reject) => {
      const request = http.request(
        {
          hostname: "127.0.0.1",
          port: 12890,
          localAddress: ip ?? "127.0.0." + ((this.peer++ % 240) + 1),
          path,
          method,
          agent: false,
          headers: {
            Connection: "close",
            ...(bytes
              ? {
                  "Content-Type": "application/json",
                  "Content-Length": bytes.length,
                }
              : {}),
            ...headers,
          },
        },
        (response) => {
          const chunks = [];
          response.on("data", (chunk) => chunks.push(chunk));
          response.on("error", reject);
          response.on("end", () => {
            const bytes = Buffer.concat(chunks);
            const result = {
              status: response.statusCode,
              headers: response.headers,
              bytes,
            };
            if (
              response.headers["content-type"]?.startsWith("application/json")
            )
              result.body = JSON.parse(bytes.toString());
            assert.equal(
              Number(response.headers["content-length"]),
              bytes.length,
            );
            assert.equal(response.headers.connection, "close");
            assert.deepEqual(
              Object.keys(response.headers).filter((name) =>
                name.startsWith("access-control-"),
              ),
              [],
            );
            resolve(result);
          });
        },
      );
      request.on("error", reject);
      request.setTimeout(10000, () =>
        request.destroy(Error("Test HTTP timeout")),
      );
      request.end(bytes);
    });
  }
  post(layout = toc, options = {}) {
    return this.request("/cd/v2/lookups", {
      body: { type: "stroom-cd-toc", ...layout },
      ...options,
    });
  }
  result(layout = toc, options) {
    return this.request("/cd/v2/lookups/" + discId(layout), options);
  }
  art(layout = toc, options) {
    return this.result(layout, options);
  }
  async complete(layout = toc, options) {
    let result = await this.post(layout, options);
    await this.until(async () => {
      if (result.status !== 202) return true;
      result = await this.result(layout, options);
      return result.status !== 202;
    });
    return result;
  }
  async report(layout = toc) {
    try {
      return JSON.parse(
        await readFile(this.directory + "/" + discId(layout) + ".json", "utf8"),
      );
    } catch {
      return null;
    }
  }
  async saved(layout, check = (report) => !!report) {
    await this.until(async () => check(await this.report(layout)));
    return this.report(layout);
  }
  summaries() {
    return this.output
      .split("\n")
      .filter((line) => line.startsWith("CD monitor: "))
      .flatMap((line) => {
        try {
          const value = JSON.parse(line.slice(12));
          return value.event === "summary" ? [value] : [];
        } catch {
          return [];
        }
      });
  }
  async sample() {
    const before = this.summaries().length;
    await this.control({ sample: true });
    await this.until(() => this.summaries().length > before);
    return this.summaries().at(-1);
  }
  async stop(force = false) {
    if (
      !this.child ||
      this.child.exitCode !== null ||
      this.child.signalCode !== null
    )
      return;
    const exited = once(this.child, "exit");
    this.child.kill(force ? "SIGKILL" : "SIGTERM");
    const timer = setTimeout(() => this.child.kill("SIGKILL"), 10000);
    try {
      const [code] = await exited;
      if (!force) assert.equal(code, 0, this.output);
    } finally {
      clearTimeout(timer);
    }
  }
  async close() {
    await this.stop(true);
    await rm(this.directory, { recursive: true, force: true });
  }
}
export function cached(
  layout = toc,
  {
    artworkRetryable = false,
    title = "Cached album",
    trackTitle = (i) => "Track " + (i + 1),
    coverFile,
    retryAt = 0,
  } = {},
) {
  return {
    discId: discId(layout),
    toc: layout,
    selected: { releaseId: "cached", mediumPosition: 1 },
    artworkRetryable,
    artworkRetryAt: retryAt,
    candidates: [
      {
        id: "cached",
        title,
        artist: "Artist",
        ...(coverFile ? { coverFile } : {}),
        media: [
          {
            position: 1,
            tracks: layout.offsets.map((_, i) => ({ title: trackTitle(i) })),
          },
        ],
      },
    ],
  };
}
