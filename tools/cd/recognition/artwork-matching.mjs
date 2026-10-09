import { normalizeName } from "./matching.mjs";

const MAX_ALIAS_TEXT = 512;

// Canonical Roman spellings only, bounded to plausible compilation volumes.
const romanVolumes = Array.from({ length: 99 }, (_, i) => {
  let value = i + 1,
    result = "";
  for (const [number, letters] of [
    [90, "XC"],
    [50, "L"],
    [40, "XL"],
    [10, "X"],
    [9, "IX"],
    [5, "V"],
    [4, "IV"],
    [1, "I"],
  ]) {
    while (value >= number) {
      result += letters;
      value -= number;
    }
  }
  return result;
});

function numberedTitle(text) {
  const title = String(text ?? "").trim();
  if (title.length > MAX_ALIAS_TEXT) return null;
  const main = title.split(/\s+[-–—]\s+|\s*[(\[]/u, 1)[0].trim();
  const match =
    /^(.*?)(?:\s+(?:vol(?:ume)?\.?\s*)|\s+)([0-9]{1,2}|[ivxlcdm]+)$/iu.exec(
      main,
    );
  if (!match) return null;
  const number = /^\d+$/u.test(match[2])
    ? Number(match[2])
    : romanVolumes.indexOf(match[2].toUpperCase()) + 1;
  const prefix = match[1].replace(/[,\s]+$/u, "");
  if (number < 1 || number > 99 || !prefix) return null;
  return { prefix, number };
}

/** Preserve the volume; only a numbered series can omit its subtitle. */
export function matchingArtworkTitles(left, right) {
  const a = numberedTitle(left),
    b = numberedTitle(right);
  if (!a || !b || a.number !== b.number) return false;
  if (normalizeName(a.prefix) !== normalizeName(b.prefix)) return false;
  // Avoid matching generic numbered labels such as "Volume 4" or "Mix 4".
  return (
    normalizeName(a.prefix).length >= 6 &&
    (a.prefix.match(/[\p{L}\p{N}]+/gu)?.length ?? 0) >= 2
  );
}

/** At most two additional artwork searches, retaining the series volume. */
export function artworkSearchTitles(title) {
  const numbered = numberedTitle(title);
  if (!numbered || !matchingArtworkTitles(title, title)) return [];
  return [
    `${numbered.prefix} ${numbered.number}`,
    `${numbered.prefix} ${romanVolumes[numbered.number - 1]}`,
  ];
}

export function artworkArtist(name) {
  const text = String(name ?? "");
  return text.length > MAX_ALIAS_TEXT
    ? text
    : text.replace(/\s+(?:pres\.?|presents)\s*$/iu, "");
}

export const artworkCredit = (title) =>
  String(title ?? "").replace(/\bjunior\b/giu, "Jr");
