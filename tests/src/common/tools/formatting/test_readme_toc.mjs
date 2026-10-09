import assert from "node:assert/strict";
import { withReadmeToc } from "../../../../../tools/formatting/readme-toc.mjs";

function check(input, expected) {
  assert.equal(withReadmeToc(input), expected);
  assert.equal(
    withReadmeToc(expected),
    expected,
    "TOC updates must be idempotent",
  );
}

const toc = [
  "<!-- BEGIN TOC -->",
  "",
  "## Contents",
  "",
  "- [Build and run](#build-and-run)",
  "  - [Launch](#launch)",
  "- [License](#license)",
  "",
  "<!-- END TOC -->",
].join("\n");
const sections = "## Build and run\n\n### Launch\n\n## License\n";

check(`# Stroom\n\n${sections}`, `# Stroom\n\n${toc}\n\n${sections}`);

const intro = [
  "<picture>",
  '  <img alt="Stroom logo" src="bin/logo-light.svg">',
  "</picture>",
  "",
  "A PlayStation 2 audio player.",
  "",
  "![Streaming demo](bin/stream.gif)",
].join("\n");

check(`${intro}\n\n${sections}`, `${intro}\n\n${toc}\n\n${sections}`);
check(sections, `${toc}\n\n${sections}`);

const example = "```markdown\n# Example title\n## Example section\n```";
check(`${example}\n\n${sections}`, `${example}\n\n${toc}\n\n${sections}`);

check(
  `# Stroom\n\n${toc}\n\n${intro}\n\n${sections}`,
  `# Stroom\n\n${toc}\n\n${intro}\n\n${sections}`,
);
check("# Stroom\n\nNo sections.\n", "# Stroom\n\nNo sections.\n");

const duplicateSections =
  "## Build\n\n~~~markdown\n## Build\n~~~\n\n## Build\n";
const duplicateToc =
  "<!-- BEGIN TOC -->\n\n## Contents\n\n- [Build](#build)\n- [Build](#build-1)\n\n<!-- END TOC -->";
check(duplicateSections, `${duplicateToc}\n\n${duplicateSections}`);

console.log("README TOC tests passed");
