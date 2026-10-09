import {
  ariacast,
  diagnostic,
  diagnosticEvents,
  diagnosticArtworkPhaseNames,
} from "../contracts/load.mjs";
import dgram from "node:dgram";
import { isIPv4 } from "node:net";
import { mkdir, writeFile, rename } from "node:fs/promises";
import path from "node:path";

const [address, directory, action] = process.argv.slice(2);
if (!isIPv4(address ?? "") || !directory) {
  console.error("Usage: make diagnostic PS2_IP=<console IPv4 address>");
  process.exit(1);
}

const socket = dgram.createSocket("udp4");
await new Promise((resolve, reject) => {
  socket.once("error", reject);
  socket.connect(ariacast.ARIA_DISCOVERY_PORT, address, resolve);
});

async function request(command, matches) {
  return new Promise((resolve, reject) => {
    let attempts = 0;
    let timer;
    const finish = (error, value) => {
      clearTimeout(timer);
      socket.off("message", message);
      socket.off("error", failed);
      if (error) reject(error);
      else resolve(value);
    };
    const failed = (error) => finish(error);
    const message = (data) => {
      let reply;
      try {
        reply = JSON.parse(data.toString());
      } catch {
        return;
      }
      if (
        reply.diagnosticVersion === diagnostic.DIAGNOSTIC_CAPTURE_VERSION &&
        matches(reply)
      )
        finish(null, reply);
    };
    const send = () => {
      if (++attempts > 5) {
        finish(new Error(`No matching reply to ${command}`));
        return;
      }
      socket.send(Buffer.from(command), (error) => {
        if (error) finish(error);
      });
      timer = setTimeout(send, 1000);
    };
    socket.on("message", message);
    socket.on("error", failed);
    send();
  });
}

const kinds = Object.fromEntries(
  diagnosticEvents.map(({ id, name, fields }) => [id, [name, ...fields]]),
);

try {
  if (action === "clear") {
    await request(
      diagnostic.DIAGNOSTIC_CAPTURE_CLEAR_QUERY,
      (r) => r.cleared === true,
    );
    console.log("Saved PS2 diagnostic slots cleared; playback is unchanged.");
  } else {
    let manifest = await request(diagnostic.DIAGNOSTIC_CAPTURE_QUERY, (r) =>
      Array.isArray(r.events),
    );
    for (let i = 0; i < 4 && manifest.events.some((e) => !e.complete); ++i) {
      await new Promise((resolve) => setTimeout(resolve, 750));
      manifest = await request(diagnostic.DIAGNOSTIC_CAPTURE_QUERY, (r) =>
        Array.isArray(r.events),
      );
    }
    if (manifest.events.some((e) => !e.complete))
      throw new Error("Capture is still collecting; retry after two seconds.");
    console.log(
      `IOP diagnostic status: ${manifest.iopStatus}; observations: ${manifest.iopRecords ?? "unknown"}.`,
    );
    console.log(
      manifest.sackKnown
        ? `SACK: compiled=${Boolean(manifest.sackEnabled)}, negotiated=${Boolean(manifest.sackNegotiated)} (current audio session).`
        : "SACK state: unknown; no state observation received for this session.",
    );
    if (manifest.iopStatus === -3)
      console.log(
        "IOP audio socket selection failed; the capture cannot identify TCP delivery stalls.",
      );
    else if (manifest.iopStatus !== 1)
      console.log(
        "IOP diagnostic is unavailable: boot the diagnostic-enabled PS2Link to obtain both sides of the recording.",
      );
    if (!manifest.events.length) {
      console.log(
        "No captured audio shortage. Reproduce a chop, then run make diagnostic again.",
      );
    } else {
      const events = [];
      for (const event of manifest.events) {
        if (
          !Number.isInteger(event.count) ||
          event.count < 0 ||
          event.count > diagnostic.DIAGNOSTIC_CAPTURE_RECORDS ||
          !Number.isInteger(event.id) ||
          event.id <= 0
        )
          throw new Error("Invalid event manifest");
        const records = [];
        while (records.length < event.count) {
          const offset = records.length;
          const page = await request(
            `${diagnostic.DIAGNOSTIC_CAPTURE_QUERY} ${event.id} ${offset}`,
            (r) => r.id === event.id && r.offset === offset,
          );
          if (page.unavailable)
            throw new Error(
              "Event was cleared or console restarted during retrieval; no partial file saved",
            );
          if (
            !Array.isArray(page.records) ||
            !page.records.length ||
            page.records.length > diagnostic.DIAGNOSTIC_CAPTURE_PAGE_RECORDS ||
            offset + page.records.length > event.count ||
            page.records.some(
              (r) =>
                !Array.isArray(r) ||
                r.length !== diagnostic.DIAGNOSTIC_CAPTURE_RECORD_WORDS ||
                r.some((v) => !Number.isInteger(v) || v < 0 || v > 0xffffffff),
            )
          )
            throw new Error("Invalid diagnostic page");
          records.push(...page.records);
          // Limit retrieval traffic while the console is still playing.
          await new Promise((resolve) => setTimeout(resolve, 20));
        }
        const decoded = records.map(([at, kind, ...values]) => {
          const [name, ...fields] = kinds[kind] ?? [`unknown:${kind}`];
          return {
            relativeMs: (at - event.trigger) | 0,
            kind: name,
            ...(kind === diagnostic.DIAGNOSTIC_CAPTURE_COVER
              ? {
                  phaseName:
                    diagnosticArtworkPhaseNames[
                      values[diagnostic.DIAGNOSTIC_CAPTURE_COVER_PHASE]
                    ] ?? "unknown",
                  beginRelativeMs:
                    (at -
                      values[diagnostic.DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] -
                      event.trigger) |
                    0,
                }
              : {}),
            ...Object.fromEntries(
              fields.map((field, index) => [field, values[index]]),
            ),
          };
        });
        events.push({ ...event, records, decoded });
      }
      await mkdir(directory, { recursive: true });
      const file = path.join(
        directory,
        `diagnostic-${new Date().toISOString().replaceAll(":", "-")}.json`,
      );
      const output = {
        version: 1,
        address,
        capturedAt: new Date().toISOString(),
        missed: manifest.missed,
        iopStatus: manifest.iopStatus,
        iopRecords: manifest.iopRecords,
        sackKnown: manifest.sackKnown,
        sackEnabled: manifest.sackEnabled,
        sackNegotiated: manifest.sackNegotiated,
        iopLost: manifest.iopLost,
        events,
      };
      await writeFile(`${file}.tmp`, JSON.stringify(output));
      await rename(`${file}.tmp`, file);
      console.log(`Saved ${events.length} event(s) to ${file}`);
      for (const event of events)
        console.log(
          `Event ${event.id}: ${event.count} observations, ${event.truncated} omitted, prehistory limited: ${Boolean(event.historyLimited)}`,
        );
      if (manifest.missed)
        console.log(
          `${manifest.missed} additional trigger(s) could not be retained.`,
        );
      console.log(
        "Events remain on the PS2. Use make diagnostic-clear only after saving them.",
      );
    }
  }
} finally {
  socket.close();
}
