// Doxygen block commands belong on separate comment lines. Inline commands such
// as @ref and @c are deliberately excluded.
const verbatimCommands = new Map([
  ["code", "endcode"],
  ["verbatim", "endverbatim"],
  ["dot", "enddot"],
  ["msc", "endmsc"],
  ["startuml", "enduml"],
]);
const blockCommands = new Set([
  "brief",
  "details",
  "param",
  "tparam",
  "return",
  "returns",
  "retval",
  "note",
  "warning",
  "attention",
  "pre",
  "post",
  "invariant",
  "throws",
  "exception",
  "see",
  "sa",
  "since",
  "deprecated",
  "todo",
  "bug",
  "test",
  "author",
  "authors",
  "version",
  "date",
  "copyright",
  "file",
  "class",
  "struct",
  "enum",
  "typedef",
  "fn",
  "var",
  "def",
  "name",
  "ingroup",
  "defgroup",
  "addtogroup",
  "example",
  "par",
  "paragraph",
  "section",
  "subsection",
  "subsubsection",
  ...verbatimCommands.keys(),
  ...verbatimCommands.values(),
]);

/**
 * Find Doxygen block commands that follow prose or another command on a line.
 * Tokenize C/C++ comments and literals together so quoted examples, raw strings
 * and ordinary comments cannot introduce false documentation blocks.
 * @param {string} text Complete C/C++ source.
 * @returns {{line: number, command: string}[]} Invalid commands and source lines.
 */
export function doxygenIssues(text) {
  const tokens =
    /(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\\r\n])+'|\/\/(?:\\\r?\n|[^\r\n])*|\/\*[\s\S]*?\*\//g;
  const issues = [];
  for (const token of text.matchAll(tokens)) {
    const comment = token[0];
    if (!/^\/\*[*!]/.test(comment)) continue;
    const firstLine = text.slice(0, token.index).split("\n").length;
    let verbatimEnd;
    for (const [offset, line] of comment.split(/\r?\n/).entries()) {
      const commands =
        /(?<![\\@])[@\\]([A-Za-z]+)(?:\[[^\]\r\n]*\])?(?=\s|\{|$)/g;
      for (const command of line.matchAll(commands)) {
        const name = command[1];
        if (verbatimEnd && name !== verbatimEnd) continue;
        if (!blockCommands.has(name)) continue;
        const prefix = line.slice(0, command.index);
        if (!/^\s*(?:\/\*[*!]<?|\*)?\s*$/.test(prefix)) {
          issues.push({ line: firstLine + offset, command: command[0] });
        }
        verbatimEnd = verbatimEnd ? undefined : verbatimCommands.get(name);
      }
    }
  }
  return issues;
}
