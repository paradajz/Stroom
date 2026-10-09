import { parseArgs } from "node:util";

export const captureUsage = `Capture options:
  --input DEVICE  Audio input name or index (default: system default input)
  --left CHANNEL  Select the left input channel (starts at 1)
  --right CHANNEL Select the right input channel (starts at 1)
Without channel options, FFmpeg converts the input to stereo.
When selecting channels, omitted left/right values default to 1/2.`;

export function parseCaptureArgs(args) {
  const options = parseArgs({
    args,
    options: {
      input: { type: "string", default: "default" },
      left: { type: "string" },
      right: { type: "string" },
      help: { type: "boolean", short: "h" },
    },
    allowPositionals: true,
  });
  const { input, left, right, help } = options.values;
  if (help) return options;
  if (!input.trim() || /[\x00-\x1f\x7f]/u.test(input))
    throw new Error("Supply an audio input name or index with --input.");
  for (const [name, value] of [
    ["left", left],
    ["right", right],
  ]) {
    if (
      value !== undefined &&
      (!/^\d+$/u.test(value) ||
        !Number.isSafeInteger(Number(value)) ||
        Number(value) < 1)
    )
      throw new Error(`--${name} must be a positive whole channel number.`);
  }
  return options;
}

export function captureOptionArgs(values) {
  return ["input", "left", "right"].flatMap((name) =>
    values[name] === undefined ? [] : [`--${name}`, values[name]],
  );
}
