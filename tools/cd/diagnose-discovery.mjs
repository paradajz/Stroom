#!/usr/bin/env node
import { spawn, spawnSync } from "node:child_process";
import { networkInterfaces } from "node:os";
import { isIPv4 } from "node:net";
import { setTimeout as delay } from "node:timers/promises";
import { cdStream as wire } from "../contracts/load.mjs";
import { discoveryBroadcasts, discoverCdSources } from "./stream.mjs";

// Read-only firewall inspection. Never install or modify firewall rules here.
function inspectFirewall(device, root) {
  const read = (command, args) =>
    spawnSync(root ? command : "sudo", root ? args : ["-n", command, ...args], {
      encoding: "utf8",
      timeout: 5000,
      maxBuffer: 2 * 1024 * 1024,
    });
  const print = (result) => {
    const output = (result.stdout ?? "").trim();
    if (output)
      console.log(
        output.length > 20000
          ? output.slice(0, 20000) + "\n[output truncated]"
          : output,
      );
  };
  console.log("\nRead-only firewall inspection:");
  const firewalld = read("firewall-cmd", ["--state"]);
  if (firewalld.status === 0 && firewalld.stdout.trim() === "running") {
    console.log("Manager: firewalld");
    const binding = read("firewall-cmd", ["--get-zone-of-interface=" + device]);
    let zone = binding.status === 0 ? binding.stdout.trim() : "";
    if (!zone || zone === "no zone") {
      const fallback = read("firewall-cmd", ["--get-default-zone"]);
      zone = fallback.status === 0 ? fallback.stdout.trim() : "";
    }
    console.log("Interface zone: " + (zone || "unknown"));
    print(read("firewall-cmd", ["--get-active-zones"]));
    if (zone) print(read("firewall-cmd", ["--zone=" + zone, "--list-all"]));
    return;
  }
  const ufw = read("ufw", ["status", "verbose"]);
  if (ufw.status === 0 && /Status: active/.test(ufw.stdout)) {
    console.log("Manager: UFW");
    print(ufw);
    return;
  }
  const nft = read("nft", ["list", "ruleset"]);
  if (nft.status === 0 && nft.stdout.trim()) {
    console.log("Manager: not identified; nftables rules:");
    print(nft);
    return;
  }
  const iptables = read("iptables", ["-S"]);
  if (iptables.status === 0 && iptables.stdout.trim()) {
    console.log("Manager: not identified; iptables rules:");
    print(iptables);
    return;
  }
  console.log(
    "No active firewall rules could be read. Further packet inspection is needed.",
  );
}

const usage = `Usage: node tools/cd/diagnose-discovery.mjs PS2_IP [--firewall-only]
Linux: capture UDP discovery traffic and compare broadcast with direct queries.
Leave the working audio listener running in its terminal.
Default: requires ip, tcpdump and timeout; requests sudo for packet capture and
read-only firewall inspection. --firewall-only skips capture and discovery.
No network or firewall settings are changed.`;
const host = process.argv[2];
const firewallOnly = process.argv[3] === "--firewall-only";
if (host === "--help" || host === "-h") {
  console.log(usage);
} else if (
  (process.argv.length !== 3 && !(process.argv.length === 4 && firewallOnly)) ||
  !isIPv4(host ?? "")
) {
  console.error(usage);
  process.exitCode = 1;
} else {
  let capture;
  let captured;
  let stopping = false;
  const controller = new AbortController();
  const stop = (signal, code) => {
    stopping = true;
    process.exitCode = code;
    controller.abort();
    capture?.kill(signal);
  };
  process.once("SIGINT", () => stop("SIGINT", 130));
  process.once("SIGTERM", () => stop("SIGTERM", 143));
  try {
    const route = spawnSync("ip", ["-j", "-4", "route", "get", host], {
      encoding: "utf8",
    });
    if (route.error || route.status !== 0)
      throw Error(
        "Cannot read the IPv4 route. This script requires Linux and iproute2.",
      );
    const [path] = JSON.parse(route.stdout);
    if (!path?.dev) throw Error("No interface found for the PS2 route.");
    const entries = networkInterfaces()[path.dev] ?? [];
    const broadcasts = discoveryBroadcasts({ [path.dev]: entries });
    const broadcast =
      broadcasts.find((value) => value !== "255.255.255.255") ??
      "255.255.255.255";
    if (!firewallOnly)
      for (const [command, args] of [
        ["tcpdump", ["--version"]],
        ["timeout", ["--version"]],
      ]) {
        const check = spawnSync(command, args, { stdio: "ignore" });
        if (check.error || check.status !== 0)
          throw Error("Install " + command + " and rerun this script.");
      }
    const root = process.getuid?.() === 0;
    if (!root) {
      console.log(
        firewallOnly
          ? "Authorizing read-only firewall inspection..."
          : "Authorizing capture and read-only firewall inspection (queries run as your normal user)...",
      );
      const authorization = spawnSync("sudo", ["-v"], { stdio: "inherit" });
      if (authorization.error || authorization.status !== 0)
        throw Error("Diagnostic authorization failed.");
    }
    console.log(
      "Interface: " +
        path.dev +
        "; PC: " +
        (path.prefsrc ?? path.src ?? "unknown"),
    );
    console.log(
      "PS2: " +
        host +
        "; broadcast: " +
        broadcast +
        "; UDP port: " +
        wire.CD_STREAM_PORT,
    );
    if (!firewallOnly) {
      console.log(
        "Capturing for seven seconds; no network settings are changed.",
      );
      const command = [
        "timeout",
        "7",
        "tcpdump",
        "-l",
        "-nn",
        "-i",
        path.dev,
        "udp port " + wire.CD_STREAM_PORT,
      ];
      capture = spawn(
        root ? command[0] : "sudo",
        root ? command.slice(1) : command,
        { stdio: "inherit" },
      );
      captured = new Promise((resolve, reject) => {
        capture.once("error", reject);
        capture.once("close", (code, signal) => resolve({ code, signal }));
      });
      // Handle an early capture failure even while the queries are in progress.
      captured.catch(() => {});
      await delay(1000, undefined, { signal: controller.signal });
      for (const [name, address] of [
        ["Broadcast", broadcast],
        ["Direct UDP", host],
      ]) {
        if (stopping) break;
        console.log("\n" + name + " query to " + address + ":");
        const sources = await discoverCdSources({
          broadcast: address,
          signal: controller.signal,
        });
        for (const source of sources)
          console.log(source.name + "\t" + source.host + ":" + source.port);
        if (!sources.length) console.log("No discovery replies received.");
      }
      const result = await captured;
      if (!stopping && result.code !== 0 && result.code !== 124)
        throw Error(
          "Packet capture failed (exit " +
            result.code +
            ", signal " +
            result.signal +
            ").",
        );
    }
    if (!stopping) inspectFirewall(path.dev, root);
    if (!stopping)
      console.log(
        "\nDone. Paste this terminal's complete output for diagnosis.",
      );
  } catch (error) {
    if (!stopping) {
      console.error(error.message);
      process.exitCode = 1;
    }
  } finally {
    if (capture?.exitCode === null && capture?.signalCode === null)
      capture.kill("SIGTERM");
    if (captured) await captured.catch(() => {});
  }
}
