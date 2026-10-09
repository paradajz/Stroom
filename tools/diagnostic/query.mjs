import {
  ariacast,
  diagnostic,
  metadataActionNames,
} from "../contracts/load.mjs";
import dgram from "node:dgram";
import { isIPv4 } from "node:net";

const address = process.argv[2];
if (!isIPv4(address ?? "")) {
  console.error("Usage: make diagnostics PS2_IP=<console IPv4 address>");
  process.exit(1);
}

const socket = dgram.createSocket("udp4");
const port = ariacast.ARIA_DISCOVERY_PORT;
const artwork = process.argv[3] === "artwork";
const driver = process.argv[3] === "driver";
const metadata = process.argv[3] === "metadata";
const query = Buffer.from(
  driver
    ? diagnostic.DIAGNOSTIC_DRIVER_QUERY
    : metadata
      ? diagnostic.DIAGNOSTIC_METADATA_QUERY
      : artwork
        ? diagnostic.DIAGNOSTIC_ARTWORK_QUERY
        : diagnostic.DIAGNOSTIC_QUERY,
);
let attempts = 0;
let timer;
let finished = false;

function finish(error) {
  if (finished) return;
  finished = true;
  clearTimeout(timer);
  if (error) {
    console.error(error);
    process.exitCode = 1;
  }
  socket.close();
}

function request() {
  if (++attempts > 3) {
    finish(
      "No UDP diagnostics reply. Check the console address and loaded build.",
    );
    return;
  }
  socket.send(query, port, address, (error) => {
    if (error) finish(error.message);
  });
  timer = setTimeout(request, 2000);
}

socket.on("error", (error) => finish(error.message));
socket.on("message", (data, peer) => {
  if (peer.address !== address || peer.port !== port) return;
  if (metadata) {
    const raw = data.subarray(1).toString();
    let request = raw;
    try {
      request = JSON.parse(raw);
    } catch {
      /* Keep malformed input readable. */
    }
    console.log(
      JSON.stringify(
        {
          action: data.length
            ? (metadataActionNames[data[0]] ?? `unknown (${data[0]})`)
            : "no update received",
          bytes: Math.max(0, data.length - 1),
          request,
        },
        null,
        2,
      ),
    );
    finish();
    return;
  }
  try {
    const stats = JSON.parse(data.toString());
    if (
      (driver
        ? stats.driverDiagnosticVersion
        : artwork
          ? stats.artworkDiagnosticVersion
          : stats.diagnosticVersion) !== 1
    )
      return;
    console.log(JSON.stringify(stats, null, 2));
    finish();
  } catch {
    // Ignore unrelated or incomplete datagrams and let the bounded retry run.
  }
});
socket.bind(0, request);
