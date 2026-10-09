import {
  normalizeName as normalize,
  artistName,
  matchingArtists,
} from "./matching.mjs";
import { request } from "./http.mjs";

const MAX_RELEASES = 8;
const REQUEST_INTERVAL_MS = 3000;
// Keep the volume number significant while accepting catalog abbreviations.
const releaseTitle = (text) =>
  String(text ?? "").replace(/\b(?:volume|vol)\.?\s*(?=\d)/giu, "");
const trackName = (text) =>
  normalize(
    String(text ?? "")
      .toLowerCase()
      .replace(/\b(?:version|verzija|19\d{2}|20\d{2})\b/gu, ""),
  );
const displayArtists = (artists = []) =>
  artists.map((artist) => artist.name.replace(/\s*\(\d+\)$/u, "")).join(", ");

function matchingReleaseArtists(seed, artists = []) {
  if (!artists.length) return false;
  const joined = artists
    .map(
      (artist, i) =>
        artist.name.replace(/\s*\(\d+\)$/u, "") +
        (i + 1 < artists.length ? (artist.join ?? ", ") : ""),
    )
    .join("");
  return matchingArtists(
    seed,
    {
      artist: displayArtists(artists),
      artists: artists.map((artist) => artist.name),
    },
    [joined],
  );
}

function milliseconds(text) {
  const match = /^(\d+):([0-5]\d)$/u.exec(text ?? "");
  return match ? (Number(match[1]) * 60 + Number(match[2])) * 1000 : null;
}

/** Preserve explicit disc numbers; reject ambiguous, gapped or nested track lists. */
function releaseDiscs(tracklist = []) {
  const discs = new Map();
  let numbered;
  for (const track of tracklist.filter((item) => item.type_ === "track")) {
    const match = /^(?:(\d+)[.-])?(\d+)$/.exec(String(track.position));
    if (!match || track.sub_tracks) return [];
    const explicit = match[1] !== undefined;
    if (numbered !== undefined && numbered !== explicit) return [];
    numbered = explicit;
    const position = explicit ? Number(match[1]) : 1;
    const number = Number(match[2]);
    if (!Number.isSafeInteger(position) || position < 1) return [];
    const tracks = discs.get(position) ?? [];
    if (number !== tracks.length + 1) return [];
    tracks.push(track);
    discs.set(position, tracks);
  }
  return [...discs].map(([position, tracks]) => ({
    position,
    tracks,
    numbered,
  }));
}

/** Provider metadata seeds searches; matching CD releases supply their own timings. */
export async function verifyDiscogs(seeds, { userAgent }, cached = []) {
  const candidates = [],
    raw = [],
    errors = [];
  const fetched = new Map(
    cached
      .filter((item) => item.release)
      .map((item) => [item.release.id, item.release]),
  );
  for (const item of cached) {
    if (item.release) raw.push(item);
    if (item.releaseId && item.error) {
      fetched.set(item.releaseId, null);
      raw.push(item);
      errors.push({
        releaseId: item.releaseId,
        message: item.error,
        retryable: item.retryable !== false,
        ...(item.retryAt ? { retryAt: item.retryAt } : {}),
      });
    }
  }
  for (const seed of seeds) {
    let results;
    try {
      // Broad discovery is only a fallback for an empty exact search. Every
      // returned release still passes full artist/title/track verification.
      for (const relaxed of [false, true]) {
        const previous = cached.find(
          (item) =>
            item.search &&
            Boolean(item.relaxed) === relaxed &&
            normalize(item.search.title) === normalize(seed.title) &&
            artistName(item.search.artist) === artistName(seed.artist),
        );
        if (previous) {
          results = previous.response;
        } else {
          const params = new URLSearchParams({
            ...(!relaxed ? { artist: seed.artist } : {}),
            release_title: relaxed ? releaseTitle(seed.title) : seed.title,
            type: "release",
            format: "CD",
            per_page: String(MAX_RELEASES),
          });
          const search = await request(
            `https://api.discogs.com/database/search?${params}`,
            userAgent,
            REQUEST_INTERVAL_MS,
          );
          results = search ? JSON.parse(search.bytes) : { results: [] };
        }
        raw.push({
          search: seed,
          ...(relaxed ? { relaxed: true } : {}),
          response: results,
        });
        if (!Array.isArray(results.results))
          throw Error("Invalid Discogs search response");
        if (results.results.length) break;
      }
    } catch (error) {
      errors.push({
        search: seed,
        message: error.message,
        retryable: error.retryable !== false,
        ...(error.retryAt ? { retryAt: error.retryAt } : {}),
      });
      continue;
    }
    for (const result of results.results.slice(0, MAX_RELEASES)) {
      if (!Number.isSafeInteger(result.id) || result.id <= 0) continue;
      try {
        let release = fetched.get(result.id);
        if (!fetched.has(result.id)) {
          if (fetched.size >= MAX_RELEASES) continue;
          fetched.set(result.id, null);
          const response = await request(
            `https://api.discogs.com/releases/${result.id}`,
            userAgent,
            REQUEST_INTERVAL_MS,
          );
          if (!response) continue;
          release = JSON.parse(response.bytes);
          fetched.set(result.id, release);
          raw.push({ release });
        }
        if (!release) continue;
        if (
          !release.formats?.some((format) => format.name === "CD") ||
          normalize(releaseTitle(release.title)) !==
            normalize(releaseTitle(seed.title)) ||
          !matchingReleaseArtists(seed, release.artists)
        )
          continue;
        for (const { position, tracks, numbered } of releaseDiscs(
          release.tracklist,
        )) {
          if (tracks.length !== seed.tracks.length) continue;
          const agreeing = tracks.filter(
            (track, i) => trackName(track.title) === trackName(seed.tracks[i]),
          ).length;
          const corroborated = agreeing / tracks.length >= 0.9;
          if (!corroborated) continue;
          const id = `discogs-${result.id}${numbered ? `-disc-${position}` : ""}`;
          if (candidates.some((candidate) => candidate.id === id)) continue;
          const cover =
            release.images?.find((image) => image.type === "primary") ??
            release.images?.[0];
          const coverUrls = [cover?.uri, cover?.uri150, release.thumb].filter(
            (url) =>
              typeof url === "string" &&
              url.startsWith("https://i.discogs.com/"),
          );
          candidates.push({
            coverUrl: coverUrls[0] ?? null,
            coverUrls: [...new Set(coverUrls)],
            id,
            source: "discogs",
            title: release.title,
            artist: displayArtists(release.artists),
            artists: release.artists.map((artist) =>
              artist.name.replace(/\s*\(\d+\)$/u, ""),
            ),
            date: release.released ?? String(release.year ?? ""),
            country: release.country,
            url: `https://www.discogs.com/release/${result.id}`,
            verification: {
              seedSource: seed.source,
              seedId: seed.id,
              agreeingTrackNames: agreeing,
              trackCount: tracks.length,
            },
            media: [
              {
                position,
                exactDisc: false,
                corroboratedNames: true,
                tracks: tracks.map((track, i) => ({
                  number: String(i + 1),
                  title: track.title,
                  artist: displayArtists(track.artists),
                  milliseconds: milliseconds(track.duration),
                })),
              },
            ],
          });
        }
      } catch (error) {
        errors.push({
          releaseId: result.id,
          message: error.message,
          retryable: error.retryable !== false,
          ...(error.retryAt ? { retryAt: error.retryAt } : {}),
        });
        raw.push({
          releaseId: result.id,
          error: error.message,
          retryable: error.retryable !== false,
          ...(error.retryAt ? { retryAt: error.retryAt } : {}),
        });
      }
    }
  }
  if (!candidates.length && errors.length) {
    const error = Error(errors.map((error) => error.message).join("; "));
    error.retryable = errors.some((item) => item.retryable);
    error.retryAt = Math.max(0, ...errors.map((item) => item.retryAt ?? 0));
    throw error;
  }
  return { candidates, raw, errors };
}
