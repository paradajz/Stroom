import { createHash } from "node:crypto";

import { cd } from "../contracts/load.mjs";
const MAX_SECTORS = 100 * 60 * cd.CD_SECTORS_PER_SECOND;

/** Wire offsets are absolute CD sectors, including the 150-sector lead-in. */
export function parseToc(data) {
  if (data.length > cd.CD_LOOKUP_PACKET_BYTES)
    throw Error("TOC datagram too large");
  const toc = JSON.parse(data.toString());
  if (
    toc?.type !== cd.CD_LOOKUP_TYPE ||
    toc.version !== cd.CD_LOOKUP_VERSION ||
    toc.first !== 1 ||
    !Number.isInteger(toc.generation) ||
    toc.generation < 0 ||
    toc.generation > 0xffffffff ||
    !Array.isArray(toc.offsets) ||
    toc.offsets.length < 1 ||
    toc.offsets.length > cd.CD_MAX_TRACKS ||
    !Number.isInteger(toc.leadout) ||
    toc.leadout > MAX_SECTORS
  )
    throw Error("Invalid CD TOC");
  let previous = cd.CD_LEAD_IN_SECTORS - 1;
  for (const offset of [...toc.offsets, toc.leadout]) {
    if (!Number.isInteger(offset) || offset <= previous)
      throw Error("Invalid CD sector ordering");
    previous = offset;
  }
  return {
    type: toc.type,
    version: cd.CD_LOOKUP_VERSION,
    generation: toc.generation,
    first: 1,
    leadout: toc.leadout,
    offsets: toc.offsets,
  };
}

export function discId(toc) {
  const hex = (value, width) =>
    value.toString(16).toUpperCase().padStart(width, "0");
  let input =
    hex(toc.first, 2) + hex(toc.offsets.length, 2) + hex(toc.leadout, 8);
  for (let i = 0; i < cd.CD_MAX_TRACKS; ++i)
    input += hex(toc.offsets[i] ?? 0, 8);
  return createHash("sha1")
    .update(input, "ascii")
    .digest("base64")
    .replaceAll("+", ".")
    .replaceAll("/", "_")
    .replaceAll("=", "-");
}

export function tocQuery(toc) {
  return [toc.first, toc.offsets.length, toc.leadout, ...toc.offsets].join(" ");
}
