import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

/** Generate README navigation from headings outside fenced code blocks. */
export function withReadmeToc(markdown) {
  const content = markdown.replace(
    /(?:^|\n)<!-- BEGIN TOC -->[\s\S]*?<!-- END TOC -->(?:\n|$)/g,
    "",
  );
  const headings = [];
  const used = new Set();
  let fence;
  let titleEnd;
  let sectionStart;
  let position = 0;
  for (const line of content.split("\n")) {
    const start = position;
    position += line.length + 1;
    const marker = line.match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
    if (marker) {
      if (!fence) {
        fence = marker[1];
      } else if (
        marker[1][0] === fence[0] &&
        marker[1].length >= fence.length &&
        !marker[2].trim()
      ) {
        fence = undefined;
      }
      continue;
    }
    if (fence) continue;
    const heading = line.match(/^(#{1,6})\s+(.+?)(?:\s+#+)?\s*$/);
    if (!heading) continue;
    const label = heading[2]
      .replace(/!?\[([^\]]+)\]\([^)]*\)/g, "$1")
      .replace(/<[^>]+>/g, "")
      .replace(/[`*~]/g, "");
    const base = label
      .toLowerCase()
      .replace(/[^\p{L}\p{N}\p{M}_\- ]/gu, "")
      .replaceAll(" ", "-");
    let slug = base;
    for (let suffix = 1; used.has(slug); suffix++) {
      slug = `${base}-${suffix}`;
    }
    used.add(slug);
    if (heading[1].length === 1) {
      titleEnd ??= start + line.length;
    } else {
      sectionStart ??= start;
      headings.push({ level: heading[1].length, label, slug });
    }
  }
  if (!headings.length) return content;
  const level = Math.min(...headings.map((heading) => heading.level));
  const entries = headings.map(
    (heading) =>
      `${"  ".repeat(heading.level - level)}- [${heading.label}](#${heading.slug})`,
  );
  const toc = [
    "<!-- BEGIN TOC -->",
    "",
    "## Contents",
    "",
    ...entries,
    "",
    "<!-- END TOC -->",
  ].join("\n");
  const end = titleEnd ?? sectionStart;
  const intro = content.slice(0, end).trimEnd();
  return `${intro ? `${intro}\n\n` : ""}${toc}\n\n${content.slice(end).trimStart()}`;
}

export function updateReadmeToc(filename) {
  const original = fs.readFileSync(filename, "utf8");
  const updated = withReadmeToc(original);
  if (updated !== original) fs.writeFileSync(filename, updated);
}

if (
  process.argv[1] &&
  path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  for (const filename of process.argv.slice(2)) updateReadmeToc(filename);
}
