// Grammar helpers for the compiler's emitted straight-line C expressions.
export function splitBinary(text, op) {
  if (text[0] !== "(" || text.at(-1) !== ")") return null;
  let depth = 0;
  for (let i = 1; i < text.length - 1; ++i) {
    if (text[i] === "(") ++depth;
    else if (text[i] === ")") --depth;
    else if (!depth && text[i] === op)
      return [text.slice(1, i), text.slice(i + 1, -1)];
  }
  return null;
}
export const assignment = /^    \(void\)\(v\[(\d+)\]=milk_finite\((.*)\)\);$/;
