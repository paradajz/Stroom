import {
  contracts,
  benchmarkProfileFields,
  metadataActionNames,
  diagnosticArtworkPhaseNames,
} from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import { writeFixture, unityRunner } from "../../support/unity.mjs";

// Fixed wire expectations protect existing diagnostic clients.
assert.deepEqual(metadataActionNames, {
  0: "rejected",
  1: "update",
  2: "get",
  3: "clear",
});

assert.deepEqual(diagnosticArtworkPhaseNames, {
  1: "decode-begin",
  2: "decode-end",
  3: "upload-begin",
  4: "upload-end",
  5: "decode-release",
  6: "frame",
  7: "socket",
  8: "nonblocking",
  9: "connect",
  10: "send",
  11: "receive",
  12: "grow",
  13: "parse",
  14: "close",
  15: "worker-release",
  16: "vram",
  17: "observations-lost",
  18: "socket-error",
});
assert.equal(contracts.diagnostic.DIAGNOSTIC_CAPTURE_COVER, 9);
assert.equal(contracts.milkdrop.MILKDROP_FPS_BASELINE_MINIMUM, 29.97);
assert.equal(contracts.milkdrop.MILKDROP_FPS_HIGH_MINIMUM, 59.94);

// Compile all generated definitions against the JS values. Other protocol tests
// retain literal wire fixtures to catch accidental compatibility changes.
const profileChecks = benchmarkProfileFields.map(
  (field, index) =>
    "    TEST_ASSERT_EQUAL_STRING(" +
    JSON.stringify(field.name) +
    ", profile_names[" +
    index +
    "]);",
);
const checks = Object.values(contracts)
  .flatMap((contract) => Object.entries(contract))
  .map(([name, value]) =>
    typeof value === "string"
      ? `    TEST_ASSERT_EQUAL_STRING(${JSON.stringify(value)}, ${name});`
      : Number.isInteger(value)
        ? `    TEST_ASSERT_EQUAL_UINT(${value}, ${name});`
        : `    TEST_ASSERT_EQUAL_FLOAT(${value}, ${name});`,
  );
writeFixture(
  process.argv[2],
  `
#include "audio/common/metadata_json.h"
${Object.keys(contracts)
  .map((name) => `#include "contracts/${name}.h"`)
  .join("\n")}
#include "unity.h"

#define PROFILE_NAME(name) #name,
static const char* const profile_names[] = { BENCHMARK_PROFILE_FIELDS(PROFILE_NAME) };

static void shared_contracts_agree(void)
{
${checks.join("\n")}
    TEST_ASSERT_EQUAL_UINT(${benchmarkProfileFields.length}, sizeof(profile_names) / sizeof(profile_names[0]));
${profileChecks.join("\n")}
    TEST_ASSERT_EQUAL_UINT(0, TRACK_METADATA_INVALID);
    TEST_ASSERT_EQUAL_UINT(1, TRACK_METADATA_UPDATE);
    TEST_ASSERT_EQUAL_UINT(2, TRACK_METADATA_GET);
    TEST_ASSERT_EQUAL_UINT(3, TRACK_METADATA_CLEAR);
}
${unityRunner("shared_contracts_agree")}`,
);
