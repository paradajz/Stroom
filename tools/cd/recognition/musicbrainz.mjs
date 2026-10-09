import { cd } from "../../contracts/load.mjs";
import { request } from "./http.mjs";
import { discId, tocQuery } from "./disc.mjs";
import { monitor } from "./monitor.mjs";
import { checkSignal } from "./deadline.mjs";
import { MAX_CANDIDATES, MAX_MEDIA, reportLimit } from "./report.mjs";

import {
  rankCandidates,
  FUZZY_MAX_MEAN_MS,
  FUZZY_MAX_TRACK_MS,
} from "./ranking.mjs";

const UUID = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i;

const artist = (credits) =>
  (credits ?? [])
    .map((c) => `${c.name ?? c.artist?.name ?? ""}${c.joinphrase ?? ""}`)
    .join("");

/** Retrieve and rank MusicBrainz metadata; artwork is resolved after selection. */
export async function lookupDisc(toc, options) {
  try {
    checkSignal(options.signal);
    const result = await lookup(toc, options);
    checkSignal(options.signal);
    return result;
  } catch (error) {
    checkSignal(options.signal, error);
    monitor.providerError("musicbrainz.org", error);
    throw error;
  }
}

async function lookup(toc, { userAgent, signal }) {
  const id = discId(toc);
  const params = new URLSearchParams({
    fmt: "json",
    inc: "recordings+artist-credits",
    cdstubs: "no",
  });
  const exactUrl = `https://musicbrainz.org/ws/2/discid/${id}?${params}`;
  let response = await request(
    exactUrl,
    userAgent,
    undefined,
    undefined,
    signal,
  );
  let match = "exact";
  if (!response) {
    match = "fuzzy";
    params.set("toc", tocQuery(toc));
    response = await request(
      `https://musicbrainz.org/ws/2/discid/-?${params}`,
      userAgent,
      undefined,
      undefined,
      signal,
    );
  }
  const raw = response ? JSON.parse(response.bytes.toString()) : null;
  const releases = response ? raw?.releases : [];
  if (!Array.isArray(releases)) throw Error("Unexpected MusicBrainz response");
  if (releases.length > MAX_CANDIDATES)
    throw reportLimit(
      "MusicBrainz response exceeds " + MAX_CANDIDATES + " releases",
    );
  for (const release of releases) {
    if ((release.media?.length ?? 0) > MAX_MEDIA)
      throw reportLimit("MusicBrainz release exceeds " + MAX_MEDIA + " media");
    for (const medium of release.media ?? [])
      if ((medium.tracks?.length ?? 0) > cd.CD_MAX_TRACKS)
        throw reportLimit(
          "MusicBrainz medium exceeds " + cd.CD_MAX_TRACKS + " tracks",
        );
  }
  const candidates = releases.map((release) => ({
    source: "musicbrainz",
    id: release.id,
    title: release.title,
    artist: artist(release["artist-credit"]),
    artists: (release["artist-credit"] ?? []).map(
      (credit) => credit.artist?.name ?? credit.name ?? "",
    ),
    date: release.date,
    country: release.country,
    barcode: release.barcode,
    disambiguation: release.disambiguation,
    url: `https://musicbrainz.org/release/${release.id}`,
    coverAvailable: release["cover-art-archive"]?.front ?? null,
    coverUrl: UUID.test(release.id ?? "")
      ? `https://coverartarchive.org/release/${release.id}/front-250`
      : null,
    media: (release.media ?? []).map((medium) => ({
      position: medium.position,
      title: medium.title,
      format: medium.format,
      exactDisc: (medium.discs ?? []).some((disc) => disc.id === id),
      tracks: (medium.tracks ?? []).map((track) => ({
        number: track.number,
        title: track.title ?? track.recording?.title,
        artist: artist(
          track["artist-credit"] ?? track.recording?.["artist-credit"],
        ),
        milliseconds: track.length ?? track.recording?.length,
      })),
    })),
  }));
  const selected = rankCandidates(toc, candidates);
  return {
    discId: id,
    toc,
    lookedUpAt: new Date().toISOString(),
    match: candidates.length ? match : "none",
    candidateCount: candidates.length,
    selected,
    fuzzyLimits: {
      meanDifferenceMs: FUZZY_MAX_MEAN_MS,
      maxDifferenceMs: FUZZY_MAX_TRACK_MS,
    },
    note: !candidates.length
      ? "No matching release found"
      : !selected
        ? "No acceptable match: candidates fail track count, timing completeness or duration limits; nothing sent to PS2"
        : selected.tiedCandidates > 1
          ? "Timing tie; selected deterministically by release ID, edition unconfirmed"
          : "Best matching medium selected; edition unconfirmed",
    tracks: toc.offsets.map((offset, i) => ({
      number: i + 1,
      offset,
      sectors: (toc.offsets[i + 1] ?? toc.leadout) - offset,
      seconds:
        ((toc.offsets[i + 1] ?? toc.leadout) - offset) /
        cd.CD_SECTORS_PER_SECOND,
    })),
    candidates,
    raw,
  };
}
