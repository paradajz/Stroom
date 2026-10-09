import { ariacast } from "../contracts/load.mjs";

/** Convert interleaved PCM16LE input to stereo packets without buffering the stream. */
export function stereoPackets(input, channels, left, right) {
  if (
    ![channels, left, right].every(Number.isSafeInteger) ||
    channels < 1 ||
    channels > 64 ||
    left < 1 ||
    right < 1 ||
    left > channels ||
    right > channels
  ) {
    throw Error(
      "Use 1–64 input channels and valid 1-based left/right channel numbers",
    );
  }
  return (async function* () {
    const frameBytes = channels * ariacast.ARIA_PCM_SAMPLE_BYTES;
    const block = Buffer.alloc(ariacast.ARIA_PCM_FRAMES * frameBytes);
    let used = 0;
    function convert() {
      const output = Buffer.alloc(ariacast.ARIA_PCM_BYTES);
      for (let frame = 0; frame < used / frameBytes; ++frame) {
        output.writeInt16LE(
          block.readInt16LE(
            frame * frameBytes + (left - 1) * ariacast.ARIA_PCM_SAMPLE_BYTES,
          ),
          frame * ariacast.ARIA_PCM_FRAME_BYTES,
        );
        output.writeInt16LE(
          block.readInt16LE(
            frame * frameBytes + (right - 1) * ariacast.ARIA_PCM_SAMPLE_BYTES,
          ),
          frame * ariacast.ARIA_PCM_FRAME_BYTES +
            ariacast.ARIA_PCM_SAMPLE_BYTES,
        );
      }
      return output;
    }
    for await (const chunk of input) {
      let offset = 0;
      while (offset < chunk.length) {
        const count = Math.min(block.length - used, chunk.length - offset);
        chunk.copy(block, used, offset, offset + count);
        used += count;
        offset += count;
        if (used === block.length) {
          yield convert();
          used = 0;
        }
      }
    }
    if (used % frameBytes)
      throw Error("Input ended in the middle of a PCM sample frame");
    // AriaCast requires full packets. Pad only the final, short block with silence.
    if (used) yield convert();
  })();
}
