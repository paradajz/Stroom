import { isAbsolute } from "node:path";

/**
 * Quote a literal systemd argument with percent specifiers escaped.
 * @param {string} value Argument text; control characters are rejected.
 * @returns {string} Double-quoted argument for an ExecStart with ':' prefix.
 */
export function quoteSystemdArgument(value) {
  if (/[\x00-\x1f\x7f]/u.test(value))
    throw Error("Paths and arguments cannot contain control characters.");
  return (
    '"' +
    value
      .replaceAll("\\", "\\\\")
      .replaceAll('"', '\\"')
      .replaceAll("%", "%%") +
    '"'
  );
}

/**
 * Select an absolute XDG directory or the account's default directory.
 * @param {string} name Environment variable name.
 * @param {string} fallback Default absolute directory.
 * @returns {string} Selected directory.
 */
export function xdgDirectory(name, fallback) {
  return isAbsolute(process.env[name] ?? "") ? process.env[name] : fallback;
}
