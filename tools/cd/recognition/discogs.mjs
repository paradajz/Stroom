import {
  normalizeName as normalize,
  artistName,
  matchingArtists,
} from "./matching.mjs";
import {
  matchingArtworkTitles,
  artworkSearchTitles,
  artworkArtist,
  artworkCredit,
} from "./artwork-matching.mjs";
import { request } from "./http.mjs";
import { monitor } from "./monitor.mjs";
import { checkSignal } from "./deadline.mjs";
import { cd } from "../../contracts/load.mjs";
import {
  MAX_CANDIDATES,
  MAX_MEDIA,
  reportLimit,
  reportText,
} from "./report.mjs";

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
// Artwork can use a catalog's omitted radio-edit suffix or possessive spelling
// only when the independently ranked track durations also pass. Other mix names
// remain significant, and metadata recognition keeps the strict name comparison.
const artworkTrackName = (text) =>
  trackName(
    String(text ?? "")
      .replace(/['’]s\b/giu, "")
      .replace(/\s*[([](?:new\s+)?radio\s+edit[)\]]\s*$/giu, ""),
  );
const displayArtists = (artists = []) =>
  artists.map((artist) => artist.name.replace(/\s*\(\d+\)$/u, "")).join(", ");

function matchingReleaseArtists(seed, artists = [], aliases = false) {
  if (aliases) {
    seed = {
      ...seed,
      artist: artworkArtist(seed.artist),
      artists: seed.artists?.map(artworkArtist),
    };
    artists = artists.map((artist) => ({
      ...artist,
      name: artworkArtist(artist.name.replace(/\s*\(\d+\)$/u, "")),
    }));
  }
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
  if (tracklist.length > MAX_MEDIA * cd.CD_MAX_TRACKS)
    throw reportLimit("Discogs track list exceeds report limits");
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
    if (
      tracks.length > cd.CD_MAX_TRACKS ||
      (!discs.has(position) && discs.size >= MAX_MEDIA)
    )
      throw reportLimit("Discogs disc layout exceeds report limits");
    discs.set(position, tracks);
  }
  return [...discs].map(([position, tracks]) => ({
    position,
    tracks,
    numbered,
  }));
}

/** Provider metadata seeds searches; matching CD releases supply their own timings. */
export async function verifyDiscogs(
  seeds,
  { userAgent, artworkOnly = false, signal },
  cached = [],
) {
  checkSignal(signal);
  if (seeds.length > 5 || cached.length > 32)
    throw reportLimit("Discogs search state exceeds report limits");
  const candidates = [],
    raw = [],
    errors = [],
    rejections = [];
  const check = (error) =>
    checkSignal(signal, {
      retryAt: Math.max(
        error?.retryAt ?? 0,
        ...errors.map((item) => item.retryAt ?? 0),
      ),
    });
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
    check();
    // Artwork can try two core-title variants after unusable numbered-series
    // hits. Metadata keeps its existing first-nonempty-search policy.
    const coreTitles = artworkOnly ? artworkSearchTitles(seed.title) : [];
    const variants = [
      { relaxed: false, title: seed.title, artist: seed.artist },
      { relaxed: true, title: releaseTitle(seed.title) },
      ...coreTitles.map((title) => ({
        relaxed: true,
        title,
        artworkQuery: title,
      })),
    ].filter(
      (variant, index, all) =>
        all.findIndex(
          (item) =>
            item.title === variant.title && item.artist === variant.artist,
        ) === index,
    );
    for (const variant of variants) {
      check();
      const before = candidates.length;
      let results;
      let liveSearch = false;
      try {
        const previous = cached.find(
          (item) =>
            item.search &&
            Boolean(item.relaxed) === variant.relaxed &&
            item.artworkQuery === variant.artworkQuery &&
            normalize(item.search.title) === normalize(seed.title) &&
            artistName(item.search.artist) === artistName(seed.artist),
        );
        if (previous) {
          results = previous.response;
        } else {
          const params = new URLSearchParams({
            ...(variant.artist ? { artist: variant.artist } : {}),
            release_title: variant.title,
            type: "release",
            format: "CD",
            per_page: String(MAX_RELEASES),
          });
          const search = await request(
            `https://api.discogs.com/database/search?${params}`,
            userAgent,
            REQUEST_INTERVAL_MS,
            undefined,
            signal,
          );
          liveSearch = true;
          results = search ? JSON.parse(search.bytes) : { results: [] };
        }
        raw.push({
          search: seed,
          ...(variant.relaxed ? { relaxed: true } : {}),
          ...(variant.artworkQuery
            ? { artworkQuery: variant.artworkQuery }
            : {}),
          response: results,
        });
        reportText({ raw });
        if (!Array.isArray(results.results))
          throw Error("Invalid Discogs search response");
      } catch (error) {
        check(error);
        if (liveSearch) monitor.providerError("api.discogs.com", error);
        if (error.code === "CD_REPORT_LIMIT") throw error;
        errors.push({
          search: seed,
          message: error.message,
          retryable: error.retryable !== false,
          ...(error.retryAt ? { retryAt: error.retryAt } : {}),
        });
        break;
      }
      for (const result of results.results.slice(0, MAX_RELEASES)) {
        check();
        if (!Number.isSafeInteger(result.id) || result.id <= 0) continue;
        let liveRelease = false;
        try {
          let release = fetched.get(result.id);
          if (!fetched.has(result.id)) {
            if (fetched.size >= MAX_RELEASES) continue;
            fetched.set(result.id, null);
            const response = await request(
              `https://api.discogs.com/releases/${result.id}`,
              userAgent,
              REQUEST_INTERVAL_MS,
              undefined,
              signal,
            );
            liveRelease = true;
            if (!response) continue;
            release = JSON.parse(response.bytes);
            fetched.set(result.id, release);
            raw.push({ release });
            reportText({ raw });
          }
          if (!release) continue;
          const sameTitle =
            normalize(releaseTitle(release.title)) ===
            normalize(releaseTitle(seed.title));
          const strictLabels =
            sameTitle && matchingReleaseArtists(seed, release.artists);
          const aliasLabels =
            !strictLabels &&
            artworkOnly &&
            (sameTitle || matchingArtworkTitles(release.title, seed.title)) &&
            matchingReleaseArtists(seed, release.artists, true);
          if (
            !release.formats?.some((format) => format.name === "CD") ||
            (!strictLabels && !aliasLabels)
          ) {
            rejections.push({
              releaseId: result.id,
              reason: "Album artist, title or format differs",
            });
            continue;
          }
          const discs = releaseDiscs(release.tracklist);
          if (!discs.some((disc) => disc.tracks.length === seed.tracks.length))
            rejections.push({
              releaseId: result.id,
              reason: "Track count differs or track list is ambiguous",
            });
          for (const { position, tracks, numbered } of discs) {
            if (tracks.length !== seed.tracks.length) continue;
            const agreeing = tracks.filter(
              (track, i) =>
                trackName(track.title) === trackName(seed.tracks[i]),
            ).length;
            const creditAgreeing = artworkOnly
              ? tracks.filter((track, i) => {
                  const name = trackName(artworkCredit(track.title));
                  return (
                    Boolean(name) &&
                    name === trackName(artworkCredit(seed.tracks[i]))
                  );
                }).length
              : 0;
            const allArtworkNames =
              artworkOnly &&
              tracks.length > 0 &&
              creditAgreeing === tracks.length;
            if (aliasLabels && !allArtworkNames) {
              rejections.push({
                releaseId: result.id,
                mediumPosition: position,
                reason: "Artwork label aliases require all ordered track names",
                agreeingTrackNames: creditAgreeing,
                trackCount: tracks.length,
              });
              continue;
            }
            const artworkAliases =
              allArtworkNames && (aliasLabels || agreeing !== tracks.length);
            const corroborated = agreeing / tracks.length >= 0.9;
            const artworkAgreeing = artworkOnly
              ? tracks.filter(
                  (track, i) =>
                    artworkTrackName(track.title) ===
                    artworkTrackName(seed.tracks[i]),
                ).length
              : 0;
            const artworkVariants =
              !corroborated &&
              !artworkAliases &&
              artworkOnly &&
              tracks.length >= 10 &&
              artworkAgreeing / tracks.length >= 0.9;
            if (!corroborated && !artworkVariants && !artworkAliases) {
              rejections.push({
                releaseId: result.id,
                mediumPosition: position,
                reason: "Track names disagree",
                agreeingTrackNames: agreeing,
                trackCount: tracks.length,
              });
              continue;
            }
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
            if (candidates.length >= MAX_CANDIDATES)
              throw reportLimit(
                "Discogs response exceeds " + MAX_CANDIDATES + " candidates",
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
                ...(artworkAliases
                  ? {
                      artworkNameAliases: true,
                      agreeingArtworkTrackNames: creditAgreeing,
                    }
                  : {}),
                ...(artworkVariants
                  ? {
                      artworkNameVariants: true,
                      agreeingArtworkTrackNames: artworkAgreeing,
                    }
                  : {}),
              },
              media: [
                {
                  position,
                  exactDisc: false,
                  corroboratedNames: corroborated,
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
          check(error);
          if (liveRelease) monitor.providerError("api.discogs.com", error);
          if (error.code === "CD_REPORT_LIMIT") throw error;
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
      if (
        candidates.length > before ||
        (!coreTitles.length && results.results.length)
      )
        break;
    }
  }
  check();
  if (!candidates.length && errors.length) {
    const error = Error(errors.map((error) => error.message).join("; "));
    error.retryable = errors.some((item) => item.retryable);
    error.retryAt = Math.max(0, ...errors.map((item) => item.retryAt ?? 0));
    throw error;
  }
  return { candidates, raw, errors, rejections };
}
