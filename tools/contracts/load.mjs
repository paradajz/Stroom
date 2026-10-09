import { readFileSync, readdirSync } from "node:fs";

const values = (entries) =>
  Object.fromEntries(
    Object.entries(entries).map(([name, entry]) => {
      if (
        typeof entry.description !== "string" ||
        !entry.description.trim() ||
        !(typeof entry.value === "string" || Number.isFinite(entry.value))
      )
        throw Error("Invalid documented constant: " + name);
      return [name, entry.value];
    }),
  );
// The definition files are the registry; no separate membership list is needed.
const directory = new URL("../../shared/contracts/", import.meta.url);
const definitions = Object.fromEntries(
  readdirSync(directory)
    .filter((file) => file.endsWith(".json"))
    .sort()
    .map((file) => {
      const name = file.slice(0, -5);
      if (!/^[a-z][a-z0-9_]*$/.test(name))
        throw Error("Invalid contract name: " + name);
      return [name, JSON.parse(readFileSync(new URL(file, directory), "utf8"))];
    }),
);
export const contracts = Object.fromEntries(
  Object.entries(definitions).map(([name, definition]) => [
    name,
    values(definition.constants ?? definition),
  ]),
);
// Named exports preserve the consumer API; generation uses the full registry.
export const {
  audio,
  aria: ariacast,
  metadata,
  artwork,
  cd,
  milkdrop,
  diagnostic,
  display,
} = contracts;
// Profile names become C members and JSON keys; groups select collector totals.
export const benchmarkProfileFields = definitions.benchmark.profileFields;
const profileNames = new Set();
if (!Array.isArray(benchmarkProfileFields) || !benchmarkProfileFields.length)
  throw Error("Missing benchmark profile fields");
for (const field of benchmarkProfileFields) {
  if (
    typeof field.name !== "string" ||
    !/^[a-z][a-z0-9_]*$/.test(field.name) ||
    profileNames.has(field.name) ||
    !["wave", "update", "render"].includes(field.group) ||
    typeof field.description !== "string" ||
    !field.description.trim()
  )
    throw Error("Duplicate or invalid benchmark profile field");
  profileNames.add(field.name);
  Object.freeze(field);
}
Object.freeze(benchmarkProfileFields);
const metadataDefinition = definitions.metadata;
export const metadataActionNames = {};
const metadataNames = new Set();
for (const action of metadataDefinition.actions) {
  if (
    !Number.isInteger(action.id) ||
    action.id < 0 ||
    action.id > 255 ||
    Object.hasOwn(metadataActionNames, action.id) ||
    typeof action.name !== "string" ||
    !action.name.trim() ||
    metadataNames.has(action.name) ||
    !/^METADATA_ACTION_[A-Z0-9_]+$/.test(action.constant) ||
    Object.hasOwn(metadata, action.constant) ||
    typeof action.description !== "string" ||
    !action.description.trim()
  )
    throw Error("Duplicate or invalid metadata action");
  metadata[action.constant] = action.id;
  metadataActionNames[action.id] = action.name;
  metadataNames.add(action.name);
}
Object.freeze(metadataActionNames);
// Derive these once; both the JS consumers and C generator use this module.
const fpsScale = 10 ** milkdrop.MILKDROP_BENCHMARK_FPS_DECIMALS;
const refreshHz =
  display.DISPLAY_REFRESH_NUMERATOR / display.DISPLAY_REFRESH_DENOMINATOR;
for (const setting of ["BASELINE", "HIGH"]) {
  milkdrop[`MILKDROP_FPS_${setting}_MINIMUM`] =
    Math.round(
      (refreshHz * milkdrop[`MILKDROP_FPS_${setting}`] * fpsScale) /
        milkdrop.MILKDROP_FPS_HIGH,
    ) / fpsScale;
}
ariacast.ARIA_PCM_FRAME_BYTES =
  ariacast.ARIA_PCM_CHANNELS * ariacast.ARIA_PCM_SAMPLE_BYTES;
ariacast.ARIA_PCM_BYTES =
  ariacast.ARIA_PCM_FRAMES * ariacast.ARIA_PCM_FRAME_BYTES;
ariacast.ARIA_PCM_MESSAGE_MS =
  (ariacast.ARIA_PCM_FRAMES * 1000) / audio.AUDIO_RATE;
metadata.METADATA_DEVICE_NAME_MAX_BYTES =
  metadata.METADATA_DEVICE_NAME_BYTES - 1;
cd.CD_LOOKUP_TOKEN_OFFSET = Buffer.byteLength(
  cd.CD_LOOKUP_REPLY_MAGIC,
  "ascii",
);
cd.CD_LOOKUP_ARTWORK_STATE_OFFSET =
  cd.CD_LOOKUP_TOKEN_OFFSET + cd.CD_LOOKUP_TOKEN_BYTES;
cd.CD_LOOKUP_REPLY_HEADER_BYTES =
  cd.CD_LOOKUP_ARTWORK_STATE_OFFSET + cd.CD_LOOKUP_ARTWORK_STATE_BYTES;
const diagnosticDefinition = definitions.diagnostic;
export const diagnosticArtworkPhaseNames = {};
const phaseNames = new Set();
for (const phase of diagnosticDefinition.artworkPhases) {
  if (
    !Number.isInteger(phase.id) ||
    phase.id < 1 ||
    phase.id > 255 ||
    Object.hasOwn(diagnosticArtworkPhaseNames, phase.id) ||
    typeof phase.name !== "string" ||
    !phase.name.trim() ||
    phaseNames.has(phase.name) ||
    !/^DIAGNOSTIC_ARTWORK_[A-Z0-9_]+$/.test(phase.constant) ||
    phase.constant === "DIAGNOSTIC_ARTWORK_PHASE_MAX" ||
    Object.hasOwn(diagnostic, phase.constant) ||
    typeof phase.description !== "string" ||
    !phase.description.trim()
  )
    throw Error("Duplicate or invalid artwork phase");
  diagnostic[phase.constant] = phase.id;
  diagnosticArtworkPhaseNames[phase.id] = phase.name;
  phaseNames.add(phase.name);
}
if (!phaseNames.size) throw Error("Missing artwork phases");
diagnostic.DIAGNOSTIC_ARTWORK_PHASE_MAX = Math.max(
  ...diagnosticDefinition.artworkPhases.map((phase) => phase.id),
);
Object.freeze(diagnosticArtworkPhaseNames);
export const diagnosticEvents = diagnosticDefinition.events.map((event) => ({
  ...event,
  fields: event.fields.map((field) => field.name),
}));
diagnostic.DIAGNOSTIC_IOP_ARIA_PORT = ariacast.ARIA_STREAM_PORT;
diagnostic.DIAGNOSTIC_IOP_DATA_WORDS = diagnostic.DIAGNOSTIC_CAPTURE_DATA_WORDS;
diagnostic.DIAGNOSTIC_CAPTURE_RECORD_WORDS =
  diagnostic.DIAGNOSTIC_CAPTURE_DATA_WORDS + 2;
const ids = new Set();
const names = new Set();
for (const event of diagnosticEvents) {
  if (
    !Number.isInteger(event.id) ||
    event.id < 1 ||
    ids.has(event.id) ||
    names.has(event.name) ||
    event.constant in diagnostic
  )
    throw Error("Duplicate or invalid diagnostic event");
  ids.add(event.id);
  names.add(event.name);
  if (new Set(event.fields).size !== event.fields.length)
    throw Error("Duplicate diagnostic field");
  if (event.fields.length !== diagnostic.DIAGNOSTIC_CAPTURE_DATA_WORDS)
    throw Error("Invalid diagnostic field count");
  diagnostic[event.constant] = event.id;
  event.fields.forEach((field, index) => {
    diagnostic[
      event.constant +
        "_" +
        field.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toUpperCase()
    ] = index;
  });
}

for (const contract of Object.values(contracts)) Object.freeze(contract);
Object.freeze(contracts);
for (const event of diagnosticEvents) {
  Object.freeze(event.fields);
  Object.freeze(event);
}
Object.freeze(diagnosticEvents);
