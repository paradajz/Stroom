// Supplement clang-format with spacing around declarations, includes, defines, guards and blocks.
import { readFileSync, writeFileSync } from "node:fs";

/** Preserve positions while hiding punctuation in comments, literals and macros. */
function maskCode(text) {
  const blank = (value) => value.replace(/[^\n]/g, " ");
  return text
    .replace(
      /\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,
      blank,
    )
    .replace(/^[ \t]*#(?:[^\n]*\\\n)*[^\n]*/gm, blank);
}

/** Separate local declaration and assignment groups without touching fields or macros. */
function spaceVariables(text) {
  const code = maskCode(text);
  const lines = text.split("\n");
  const clean = code.split("\n");
  const scopes = [];
  const local = [];
  let header = "";
  let parentheses = 0;
  // Distinguish executable blocks from aggregate definitions and initializers.
  for (let i = 0; i < clean.length; ++i) {
    local[i] =
      scopes.includes("function") &&
      !scopes.includes("aggregate") &&
      !scopes.includes("initializer");
    for (const ch of clean[i]) {
      if (ch === "{") {
        const prefix = header.trim();
        const aggregate =
          /\b(?:struct|union|enum)\b/.test(prefix) && !prefix.includes("=");
        const control = /\b(?:if|for|while|switch)\s*\(/.test(prefix);
        const kind =
          !scopes.length && prefix.endsWith(")") && !prefix.includes("=")
            ? "function"
            : aggregate
              ? "aggregate"
              : !control && (prefix.includes("=") || prefix.endsWith(")"))
                ? "initializer"
                : "block";
        scopes.push(kind);
        header = "";
      } else if (ch === "}") {
        scopes.pop();
        header = "";
      } else if (ch === ";" && parentheses === 0) {
        header = "";
      } else {
        if (ch === "(") ++parentheses;
        else if (ch === ")") --parentheses;
        header += ch;
      }
    }
    header += "\n";
  }
  // A declaration needs both a type and a declarator. Qualifiers, pointer types
  // and function pointers count. Assignments have an lvalue and assignment
  // operator; comparisons and assignments inside control conditions do not count.
  const declaration =
    /^(?!(?:return|goto|break|continue|case|else|if|for|while|switch|do|sizeof|_Static_assert)\b)(?:(?:static|extern|register|const|volatile|restrict|_Atomic)\s+)*(?:(?:struct|union|enum)\s+)?[A-Za-z_]\w*(?:\s+(?:const|volatile|restrict|signed|unsigned|short|long|int|char|double))*(?:\s+|\s*\*+\s*)(?:[A-Za-z_]\w*\s*(?=[=;,\[])|\(\s*\*\s*[A-Za-z_]\w*\s*\))/;
  const assignment =
    /^(?:\*+\s*)?[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*|\s*\[[^\]\n]+\])*\s*(?:=(?!=)|[+\-*/%&|^]=|<<=|>>=)/;
  const starts = new Set();
  const ends = new Set();
  for (let i = 0; i < clean.length; ++i) {
    const statement = clean[i].trimStart();
    if (
      !local[i] ||
      (!declaration.test(statement) && !assignment.test(statement))
    )
      continue;
    let depth = 0;
    let end = i;
    let complete = false;
    for (; end < clean.length && !complete; ++end) {
      for (const ch of clean[end]) {
        if ("([{".includes(ch)) ++depth;
        else if (")]}".includes(ch)) --depth;
        else if (ch === ";" && depth === 0) {
          complete = true;
          break;
        }
      }
    }
    if (!complete) continue;
    starts.add(i);
    ends.add(end - 1);
    i = end - 1;
  }
  const result = [];
  for (let i = 0; i < lines.length; ++i) {
    const previous = lines[i - 1]?.trim();
    const next = lines[i + 1]?.trim();
    if (
      starts.has(i) &&
      !ends.has(i - 1) &&
      previous &&
      !/(?:\{|:)\s*$/.test(previous) &&
      !/^(?:#|\/\/|\/\*|\*)/.test(previous)
    ) {
      result.push("");
    }
    result.push(lines[i]);
    if (ends.has(i) && !starts.has(i + 1) && next && !/^(?:}|#)/.test(next)) {
      result.push("");
    }
  }
  return result.join("\n");
}

/** Remove padding between consecutive standalone calls, including multiline calls. */
function groupCalls(text) {
  const lines = text.split("\n");
  const clean = maskCode(text).split("\n");
  const starts = new Set();
  const ends = new Set();
  for (let i = 0; i < clean.length; ++i) {
    if (
      !/^[ \t]+(?!(?:if|for|while|switch|return|sizeof|_Static_assert)\b)[A-Za-z_]\w*\s*\(/.test(
        clean[i],
      )
    )
      continue;
    let previous = i - 1;
    while (previous >= 0 && !clean[previous].trim()) --previous;
    // A call used inside an assignment or argument list is not a standalone call.
    if (previous >= 0 && !/[;{}:]\s*$/.test(clean[previous])) continue;
    let depth = 0;
    let end = i;
    let complete = false;
    let terminated = false;
    for (; end < clean.length && !terminated; ++end) {
      for (let column = 0; column < clean[end].length; ++column) {
        const ch = clean[end][column];
        if ("([{".includes(ch)) ++depth;
        else if (")]}".includes(ch)) --depth;
        else if (ch === ";" && depth === 0) {
          complete = !clean[end].slice(column + 1).trim();
          terminated = true;
          break;
        }
        if (depth < 0) break;
      }
      if (depth < 0) break;
    }
    if (!complete) continue;
    starts.add(i);
    ends.add(end - 1);
    i = end - 1;
  }
  const remove = new Set();
  for (const end of ends) {
    let next = end + 1;
    while (next < lines.length && !lines[next].trim()) ++next;
    if (starts.has(next)) {
      for (let i = end + 1; i < next; ++i) remove.add(i);
    }
  }
  return lines.filter((_line, index) => !remove.has(index)).join("\n");
}

/** Pad a terminal return while keeping return-only bodies and attached comments compact. */
function spaceFinalReturns(text) {
  const lines = text.split("\n");
  const clean = maskCode(text).split("\n");
  const pad = new Set();
  for (let i = 0; i < clean.length; ++i) {
    if (!/^[ \t]+return\b/.test(clean[i])) continue;
    let end = i;
    while (end < clean.length && !clean[end].includes(";")) ++end;
    let next = end + 1;
    while (next < clean.length && !lines[next].trim()) ++next;
    // clang-format places the function's closing brace at column zero.
    if (!/^}/.test(clean[next] || "")) continue;
    const previous = lines[i - 1]?.trim();
    if (
      previous &&
      !/\{$/.test(previous) &&
      !/^(?:#(?!\s*endif\b)|\/\/|\/\*|\*)/.test(previous)
    ) {
      pad.add(i);
    }
  }
  return lines
    .flatMap((line, index) => (pad.has(index) ? ["", line] : [line]))
    .join("\n");
}

for (const path of process.argv.slice(2)) {
  const original = readFileSync(path, "utf8");
  let text = original.replace(
    /(^[ \t]*#include[^\n]*\n)(?=[^\n])/gm,
    (line, _match, offset, source) =>
      /^[ \t]*#include\b/.test(source.slice(offset + line.length))
        ? line
        : `${line}\n`,
  );
  // Keep a define group separate from code without splitting continued macros.
  const lines = text.split("\n");
  const spaced = [];
  let inDefine = false;
  for (let i = 0; i < lines.length; ++i) {
    const line = lines[i];
    spaced.push(line);
    if (/^[ \t]*#define\b/.test(line)) inDefine = true;
    if (!inDefine || /\\$/.test(line)) continue;
    inDefine = false;
    const next = lines[i + 1];
    if (next !== undefined && next.trim() && !/^[ \t]*#define\b/.test(next)) {
      spaced.push("");
    }
  }
  text = spaced.join("\n");
  text = spaceVariables(text);
  // Separate a completed nested block from the following statement. Keep closing
  // braces together and leave attached else/catch clauses next to their block.
  // clang-format keeps a do/while closing condition on the same line as its brace.
  text = text.replace(
    /(^[ \t]+}[^\S\n]*\n)(?=[^\n])/gm,
    (line, _match, offset, source) =>
      /^[ \t]*(?:}|else\b|catch\b)/.test(source.slice(offset + line.length))
        ? line
        : `${line}\n`,
  );
  // Separate conditionals and loops from preceding statements, but keep the first
  // statement of a block next to its opening brace. Do not split macros.
  text = text.replace(
    /(^[^\n]+\n)(?=[ \t]*(?:(?:if|for|while)[ \t]*\(|do\b))/gm,
    (line) => (/(?:\{|\belse|\\)[ \t]*\n$/.test(line) ? line : `${line}\n`),
  );
  // Keep bare prototypes together; separate a prototype from following docs.
  // Exclude calls inside functions and function-pointer fields inside structs.
  const declaration = String.raw`^[A-Za-z_][^\n;{}()[\]=]*[ \t*](?!__attribute__\b)[A-Za-z_]\w*[ \t]*\([^;{}]*\);[ \t]*\n`;
  text = text.replace(
    new RegExp("(" + declaration + ")[ \t]*\n(?=" + declaration + ")", "gm"),
    "$1",
  );
  text = text.replace(
    new RegExp("(" + declaration + ")(?=/\\*\\*|/\\*!|///|//!)", "gm"),
    "$1\n",
  );
  // Also keep completed function definitions separate from subsequent comments.
  text = text.replace(/(^}[ \t]*\n)(?=[^\n])/gm, "$1\n");
  // Conditional directives hug their contents. Only a file's include guard
  // gets padding inside its opening define and closing endif.
  text = text.replace(
    /(^[ \t]*#(?:if|ifdef|ifndef|elif|else)\b[^\n]*\n)(?:[ \t]*\n)+/gm,
    "$1",
  );
  text = text.replace(/\n(?:[ \t]*\n)+(?=[ \t]*#(?:elif|else|endif)\b)/g, "\n");
  const guard = /\.(h|hpp)$/.test(path)
    ? text
        .replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, "")
        .trimStart()
        .match(/^#ifndef[ \t]+(\w+)[ \t]*\n#define[ \t]+\1\b/)
    : null;
  if (guard) {
    text = text.replace(
      /(^#ifndef[ \t]+(\w+)[ \t]*\n#define[ \t]+\2[^\n]*\n)(?=[^\n])/m,
      "$1\n",
    );
    text = text.replace(/([^\n])\n(#endif[^\n]*\n?)$/, "$1\n\n$2");
  }
  text = groupCalls(text);
  text = spaceFinalReturns(text);
  if (text !== original) writeFileSync(path, text);
}
