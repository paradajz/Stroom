import { cd } from "../contracts/load.mjs";
import { request } from "./http.mjs";

const APPLICATION = "PS2Vis-CD-Recognition";
const VERSION = "v0.1";
const ENDPOINT = "https://gnudb.gnudb.org/~cddb/cddb.cgi";
const MAX_READS = 3;
let refused = false;

/** A URL or missing contact must never turn into an anonymous CDDB hello. */
export function gnudbHello(contact) {
  if (typeof contact !== "string") return null;
  const match =
    /^([A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+)@([A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?\.[A-Za-z]{2,})$/.exec(
      contact.trim(),
    );
  return match ? `${match[1]} ${match[2]} ${APPLICATION} ${VERSION}` : null;
}

function multiline(lines) {
  const end = lines.indexOf(".", 1);
  if (end < 0) throw Error("Incomplete GnuDB response");
  return lines.slice(1, end).map((line) => line.replace(/^\.\./, "."));
}

async function command(cmd, hello, userAgent) {
  if (refused)
    throw Error("GnuDB access refused; disabled until service restart");
  const params = new URLSearchParams({ cmd, hello, proto: "6" });
  let response;
  try {
    response = await request(`${ENDPOINT}?${params}`, userAgent);
  } catch (error) {
    if (/^HTTP 40[13]:/.test(error.message)) refused = true;
    throw error;
  }
  if (!response) throw Error("GnuDB endpoint unavailable");
  const lines = response.bytes.toString("utf8").split(/\r?\n/);
  const status = Number(/^(\d{3})(?:\s|$)/.exec(lines[0])?.[1]);
  // Syntax/permission failures are not something repeated disc probes can fix.
  if (status >= 500 || status === 409) refused = true;
  return { status, lines };
}

function credit(text) {
  const split = text.indexOf(" / ");
  return split < 0
    ? null
    : [text.slice(0, split).trim(), text.slice(split + 3).trim()];
}

function entry(lines, category, id) {
  const body = multiline(lines);
  const fields = {};
  const offsets = [];
  let readingOffsets = false;
  let seconds, leadout;
  for (const line of body) {
    if (/^#\s*Track frame offsets:\s*$/.test(line)) {
      readingOffsets = true;
      continue;
    }
    if (readingOffsets) {
      const offset = /^#\s*(\d+)\s*$/.exec(line);
      if (offset) {
        offsets.push(Number(offset[1]));
        continue;
      }
      readingOffsets = false;
    }
    const length = /^#\s*Disc length:\s*(\d+)(?:\s|$)/.exec(line);
    if (length) seconds = Number(length[1]);
    const end = /^#\s*Leadout:\s*(\d+)\s*$/.exec(line);
    if (end) leadout = Number(end[1]);
    const field = /^(DTITLE|DYEAR|TTITLE\d+)=(.*)$/.exec(line);
    if (field) fields[field[1]] = (fields[field[1]] ?? "") + field[2];
  }
  for (const key of Object.keys(fields))
    fields[key] = fields[key]
      .replace(/\\([nt\\])/g, (_, c) => ({ n: "\n", t: "\t", "\\": "\\" })[c])
      .trim();
  leadout ??= seconds * cd.CD_SECTORS_PER_SECOND;
  if (
    !offsets.length ||
    offsets.length > cd.CD_MAX_TRACKS ||
    !Number.isSafeInteger(leadout) ||
    offsets.some(
      (offset, i) =>
        !Number.isSafeInteger(offset) ||
        offset < cd.CD_LEAD_IN_SECTORS ||
        offset <= (offsets[i - 1] ?? 0),
    ) ||
    leadout <= offsets.at(-1) ||
    Object.keys(fields).filter((key) => key.startsWith("TTITLE")).length !==
      offsets.length ||
    offsets.some((_, i) => !fields[`TTITLE${i}`]) ||
    !fields.DTITLE
  )
    throw Error("Incomplete or invalid GnuDB track layout");
  const [artist, title] = credit(fields.DTITLE) ?? [
    fields.DTITLE,
    fields.DTITLE,
  ];
  if (!artist || !title) throw Error("Missing GnuDB album credit");
  return {
    id: `gnudb-${category}-${id}`,
    source: "gnudb",
    artist,
    artists: [artist],
    title,
    date: fields.DYEAR || undefined,
    url: "https://gnudb.org",
    media: [
      {
        position: 1,
        exactDisc: false,
        tracks: offsets.map((offset, i) => {
          const text = fields[`TTITLE${i}`];
          const [trackArtist, trackTitle] = credit(text) ?? [artist, text];
          return {
            number: String(i + 1),
            title: trackTitle,
            artist: trackArtist,
            milliseconds:
              (((offsets[i + 1] ?? leadout) - offset) * 1000) /
              cd.CD_SECTORS_PER_SECOND,
          };
        }),
      },
    ],
  };
}

/** Query by TOC before reading server-assigned IDs; never trust a legacy ID alone. */
export async function lookupGnudb(toc, { userAgent, contact }) {
  const hello = gnudbHello(contact);
  if (!hello)
    return {
      candidates: [],
      skipped: "GnuDB requires --contact with a real email address",
    };
  const seconds = Math.floor(toc.leadout / cd.CD_SECTORS_PER_SECOND);
  const duration =
    seconds - Math.floor(toc.offsets[0] / cd.CD_SECTORS_PER_SECOND);
  const checksum = toc.offsets.reduce(
    (sum, offset) =>
      sum +
      [...String(Math.floor(offset / cd.CD_SECTORS_PER_SECOND))].reduce(
        (a, digit) => a + Number(digit),
        0,
      ),
    0,
  );
  const legacyId = (
    ((checksum % 255) * 0x1000000 + duration * 256 + toc.offsets.length) >>>
    0
  )
    .toString(16)
    .padStart(8, "0");
  const query = await command(
    `cddb query ${legacyId} ${toc.offsets.length} ${toc.offsets.join(" ")} ${seconds}`,
    hello,
    userAgent,
  );
  if (query.status === 202) return { candidates: [], errors: [] };
  let matches;
  if (query.status === 200) matches = [query.lines[0].slice(4)];
  else if (query.status === 210 || query.status === 211)
    matches = multiline(query.lines);
  else throw Error(`GnuDB query failed: ${query.lines[0]}`);
  const candidates = [],
    errors = [],
    seen = new Set();
  for (const match of matches) {
    const parsed =
      /^(blues|classical|country|data|folk|jazz|misc|newage|reggae|rock|soundtrack)\s+([a-f0-9]{8})\s+.+$/i.exec(
        match,
      );
    if (!parsed) continue;
    const [, category, id] = parsed;
    const key = `${category} ${id}`.toLowerCase();
    if (seen.has(key)) continue;
    if (seen.size >= MAX_READS) break;
    seen.add(key);
    try {
      const read = await command(`cddb read ${key}`, hello, userAgent);
      if (read.status === 401 || read.status === 403) continue;
      if (read.status !== 210)
        throw Error(`GnuDB read failed: ${read.lines[0]}`);
      candidates.push(
        entry(read.lines, category.toLowerCase(), id.toLowerCase()),
      );
    } catch (error) {
      errors.push({ message: error.message });
      // Avoid a burst of failing reads; retain earlier successful candidates.
      break;
    }
  }
  return { candidates, errors };
}
