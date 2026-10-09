export const normalizeName = (text) =>
  String(text ?? "")
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .toLowerCase()
    .replace(/[^\p{L}\p{N}]/gu, "");

/** MusicBrainz and Discogs use different names for the same compilation credit. */
export function artistName(text) {
  const name = normalizeName(String(text ?? "").replace(/\s*\(\d+\)$/u, ""));
  return name === "variousartists" ? "various" : name;
}

/** Compare complete credits without guessing artist boundaries from punctuation. */
export function matchingArtists(a, b, alternateCredits = []) {
  if (a.artists?.length && b.artists?.length) {
    const expected = a.artists.map(artistName).sort();
    const actual = b.artists.map(artistName).sort();
    return (
      expected.length === actual.length &&
      expected.every((name, i) => name && name === actual[i])
    );
  }
  // Older cache entries and CTDB only provide a combined display credit.
  const expected = artistName(a.artist);
  return (
    Boolean(expected) &&
    [b.artist, ...alternateCredits].some(
      (credit) => expected === artistName(credit),
    )
  );
}
