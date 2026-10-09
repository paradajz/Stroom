import { cdStream as wire } from "../contracts/load.mjs";

/** Summarize tcpdump -tt -nn -S output without interpreting audio payloads. */
export function streamTcpDiagnostics(host, port = wire.CD_STREAM_PORT) {
  const flows = new Map();
  const gaps = [];
  let lineNumber = 0;
  const totals = {
    packets: 0,
    dataPackets: 0,
    repeats: 0,
    duplicateAcks: 0,
    zeroWindows: 0,
    maxGapMs: 0,
    startupSessions: 0,
    maxStartupMs: 0,
  };
  const server = `${host}.${port}`;
  return {
    totals,
    gaps,
    observe(line) {
      ++lineNumber;
      const packet = line.match(
        /^(\d+\.\d+) IP (\S+) > (\S+): Flags \[([^\]]*)\], (.*)$/,
      );
      if (!packet) return [];
      const [, timestamp, from, destination, flags, fields] = packet;
      if (from !== server && destination !== server) return [];
      const outgoing = from === server;
      const peer = outgoing ? destination : from;
      let flow = flows.get(peer);
      if (!flow || flags.includes("S")) {
        flow = {};
        flows.set(peer, flow);
      }
      ++totals.packets;
      const events = [];
      const at = Number(timestamp);
      const bytes = Number(fields.match(/\blength (\d+)/)?.[1] ?? 0);
      const sequence = fields.match(/\bseq (\d+)(?::(\d+))?/);
      if (outgoing && flags.includes("S") && sequence)
        flow.streamStart = (Number(sequence[1]) + 1) >>> 0;
      if (outgoing && bytes > 0) {
        ++totals.dataPackets;
        // A captured SYN anchors the FORMAT byte range, including fragmented
        // or retransmitted FORMAT packets. Midstream captures have no anchor
        // and retain their first measured gap instead of guessing from length.
        const formatBytes =
          wire.CD_STREAM_HEADER_BYTES + wire.CD_STREAM_FORMAT_BYTES;
        const offset =
          sequence && flow.streamStart !== undefined
            ? (Number(sequence[1]) - flow.streamStart) | 0
            : undefined;
        if (
          !flow.audioStarted &&
          offset !== undefined &&
          offset >= 0 &&
          offset < formatBytes
        )
          flow.formatAt ??= at;
        const formatOnly =
          !flow.audioStarted &&
          offset !== undefined &&
          offset >= 0 &&
          offset + bytes <= formatBytes;
        if (!formatOnly && !flow.audioStarted) {
          flow.audioStarted = true;
          if (flow.formatAt !== undefined) {
            const startup = (at - flow.formatAt) * 1000;
            ++totals.startupSessions;
            totals.maxStartupMs = Math.max(totals.maxStartupMs, startup);
            events.push(
              `PS2 startup wait ${startup.toFixed(1)} ms (${peer}); audio starts here`,
            );
          }
        }
        if (!formatOnly && flow.lastData !== undefined) {
          const gap = (at - flow.lastData) * 1000;
          totals.maxGapMs = Math.max(totals.maxGapMs, gap);
          if (gap >= 200) {
            events.push(`PS2 data arrival gap ${gap.toFixed(1)} ms (${peer})`);
            gaps.push({
              milliseconds: gap,
              lineNumber,
              peer,
              previousEnd: flow.highestEnd,
              nextSequence: sequence ? Number(sequence[1]) : undefined,
              pcAck: flow.ack === undefined ? undefined : Number(flow.ack),
              ackAgeMs:
                flow.ackAt === undefined ? undefined : (at - flow.ackAt) * 1000,
              window: flow.window,
            });
            gaps.sort((a, b) => b.milliseconds - a.milliseconds);
            gaps.length = Math.min(gaps.length, 3);
          }
        }
        if (!formatOnly) flow.lastData = at;
        if (sequence?.[2] !== undefined) {
          const start = Number(sequence[1]);
          const end = Number(sequence[2]);
          // Signed modulo comparison handles TCP's 32-bit sequence wrap.
          if (
            flow.highestEnd !== undefined &&
            ((start - flow.highestEnd) | 0) < 0
          )
            ++totals.repeats;
          if (
            flow.highestEnd === undefined ||
            ((end - flow.highestEnd) | 0) > 0
          )
            flow.highestEnd = end;
        }
      } else if (!outgoing) {
        const window = fields.match(/\bwin (\d+)/)?.[1];
        const ack = fields.match(/\back (\d+)/)?.[1];
        if (window === "0") {
          ++totals.zeroWindows;
          if (flow.window !== "0")
            events.push(`PC advertised a zero receive window (${peer})`);
        }
        if (
          ack !== undefined &&
          ack === flow.ack &&
          window === flow.window &&
          bytes === 0 &&
          !/[SFR]/.test(flags)
        )
          ++totals.duplicateAcks;
        flow.ack = ack;
        if (ack !== undefined) flow.ackAt = at;
        flow.window = window;
      }
      if (/[FR]/.test(flags)) flows.delete(peer);
      return events;
    },
  };
}

/** Include both directions around the largest gaps, using an existing capture. */
export function streamTcpGapDetails(lines, analyzer) {
  const output = [
    analyzer.totals.startupSessions
      ? `Startup waits observed: ${analyzer.totals.startupSessions}; longest FORMAT-to-audio wait: ${analyzer.totals.maxStartupMs.toFixed(1)} ms (excluded from playback gaps).`
      : "Startup wait unavailable: FORMAT and first audio were not both observed with a captured TCP handshake.",
  ];
  for (const gap of analyzer.gaps) {
    output.push(
      `\nGap ${gap.milliseconds.toFixed(1)} ms (${gap.peer}), line ${gap.lineNumber}`,
    );
    output.push(
      `Highest PS2 sequence end before gap: ${gap.previousEnd ?? "unknown"}; next data sequence: ${gap.nextSequence ?? "unknown"}`,
    );
    output.push(
      `Last PC ACK before data resumed: ${gap.pcAck ?? "unknown"}; raw receive window: ${gap.window ?? "unknown"}; ACK age: ${gap.ackAgeMs?.toFixed(1) ?? "unknown"} ms`,
    );
    output.push(
      "Packets around resumption (PC ACKs in the gap appear before it):",
    );
    for (
      let i = Math.max(0, gap.lineNumber - 9);
      i < Math.min(lines.length, gap.lineNumber + 8);
      ++i
    )
      output.push(`${i + 1}: ${lines[i]}`);
  }
  if (!analyzer.gaps.length)
    output.push("No PS2 data arrival gaps of 200 ms or more found.");
  return output.join("\n");
}
