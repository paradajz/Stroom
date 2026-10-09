#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { isIPv4 } from "node:net";
import { cdStream as wire } from "../contracts/load.mjs";

const usage = `Usage: node tools/cd/allow-discovery.mjs PS2_IP
Linux/firewalld: permanently allow CD discovery replies from this PS2 only.
Finds the LAN zone and adds an IPv4 UDP source-port ${wire.CD_STREAM_PORT} exception.
Applies it immediately without reloading the firewall. Requires ip and sudo.`;
const host = process.argv[2];
if (host === "--help" || host === "-h") {
  console.log(usage);
} else if (process.argv.length !== 3 || !isIPv4(host ?? "")) {
  console.error(usage);
  process.exitCode = 1;
} else {
  try {
    const route = spawnSync("ip", ["-j", "-4", "route", "get", host], {
      encoding: "utf8",
      timeout: 5000,
    });
    if (route.error || route.status !== 0)
      throw Error(
        "Cannot read the IPv4 route. This script requires Linux and iproute2.",
      );
    const [path] = JSON.parse(route.stdout);
    if (!path?.dev) throw Error("No interface found for the PS2 route.");
    const root = process.getuid?.() === 0;
    console.log(
      "Saving a permanent exception for UDP replies from " +
        host +
        ":" +
        wire.CD_STREAM_PORT +
        " only.",
    );
    if (!root) {
      const authorization = spawnSync("sudo", ["-v"], { stdio: "inherit" });
      if (authorization.error || authorization.status !== 0)
        throw Error("Firewall authorization failed.");
    }
    const command = (args, query = false) => {
      const result = spawnSync(
        root ? "firewall-cmd" : "sudo",
        root ? args : ["-n", "firewall-cmd", ...args],
        { encoding: "utf8", timeout: 15000 },
      );
      if (
        result.error ||
        (result.status !== 0 && !(query && result.status === 1))
      )
        throw Error(
          (
            result.stderr ||
            result.stdout ||
            result.error?.message ||
            "firewall-cmd failed"
          ).trim(),
        );
      return result;
    };
    command(["--state"]);
    const binding = command(["--get-zone-of-interface=" + path.dev], true);
    let zone = binding.status === 0 ? binding.stdout.trim() : "";
    if (!zone || zone === "no zone")
      zone = command(["--get-default-zone"]).stdout.trim();
    if (!/^[A-Za-z0-9_-]+$/.test(zone))
      throw Error("Cannot identify the LAN firewall zone.");
    const rule =
      'rule family="ipv4" source address="' +
      host +
      '" source-port port="' +
      wire.CD_STREAM_PORT +
      '" protocol="udp" accept';
    const scope = ["--zone=" + zone];
    if (
      command([...scope, "--permanent", "--query-rich-rule=" + rule], true)
        .status !== 0
    )
      command([...scope, "--permanent", "--add-rich-rule=" + rule]);
    console.log("Permanent rule saved in zone " + zone + ".");
    // Replacing an existing runtime rule cancels its five-minute test timer.
    if (command([...scope, "--query-rich-rule=" + rule], true).status === 0)
      command([...scope, "--remove-rich-rule=" + rule]);
    command([...scope, "--add-rich-rule=" + rule]);
    console.log(
      "Runtime rule active without an expiry. Firewall was not reloaded.",
    );
    console.log("Automatic playback: node tools/cd/listener.mjs");
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
