import { cd } from "../contracts/load.mjs";
import { request } from "./http.mjs";

// CTDB's metadata subset uses attributes, not text nodes. Reject declarations
// and unknown entities rather than resolving external XML resources.
function attributes(text) {
  const values = {};
  const decode = (value) =>
    value.replace(/&([^;]+);/gu, (_, entity) => {
      const named = { amp: "&", quot: '"', apos: "'", lt: "<", gt: ">" };
      if (Object.hasOwn(named, entity)) return named[entity];
      if (/^#(?:[0-9]+|x[0-9a-f]+)$/iu.test(entity)) {
        const code =
          entity[1] === "x"
            ? parseInt(entity.slice(2), 16)
            : Number(entity.slice(1));
        if (code > 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff))
          return String.fromCodePoint(code);
      }
      throw Error("Unsupported CTDB XML entity");
    });
  const rest = text.replace(
    /([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/gu,
    (_, key, double, single) => {
      if (Object.hasOwn(values, key)) throw Error("Duplicate CTDB attribute");
      values[key] = decode(double ?? single);
      return "";
    },
  );
  if (rest.trim()) throw Error("Malformed CTDB attributes");
  return values;
}

export async function lookupCtdb(toc, { userAgent }) {
  const params = new URLSearchParams({
    version: "3",
    ctdb: "0",
    fuzzy: "1",
    metadata: "extensive",
    // CUETools uses LBA offsets, without the 150-sector lead-in.
    toc: [...toc.offsets, toc.leadout]
      .map((value) => value - cd.CD_LEAD_IN_SECTORS)
      .join(":"),
  });
  const response = await request(
    `https://db.cue.tools/lookup2.php?${params}`,
    userAgent,
  );
  if (!response) return { candidates: [], raw: null };
  const raw = response.bytes.toString("utf8");
  if (
    /<!/u.test(raw) ||
    !/^\s*(?:<\?xml[^?]*\?>\s*)?<ctdb\b[^>]*>[\s\S]*<\/ctdb>\s*$/u.test(raw)
  )
    throw Error("Invalid CTDB XML response");
  const candidates = [];
  for (const match of raw.matchAll(
    /<metadata\b([^>]*)>([\s\S]*?)<\/metadata>/gu,
  )) {
    const meta = attributes(match[1]);
    const tracks = [
      ...match[2].matchAll(/<track\b([^>]*?)(?:\/>|>[\s\S]*?<\/track>)/gu),
    ].map((track) => attributes(track[1]));
    if (
      meta.artist &&
      meta.album &&
      tracks.length === toc.offsets.length &&
      tracks.every((t) => t.name)
    )
      candidates.push({
        artist: meta.artist,
        title: meta.album,
        source: meta.source,
        id: meta.id,
        tracks: tracks.map((t) => t.name),
      });
  }
  return { candidates: candidates.slice(0, 5), raw };
}
