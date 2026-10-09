import { audio, ariacast } from "../contracts/load.mjs";
import { streamPCM } from "../aria/ariacast.mjs";

/** Deterministic one-second, stereo PCM16/48k loop, sent through real AriaCast. */
export function generatedPCM() {
  const pcm = Buffer.alloc(audio.AUDIO_RATE * ariacast.ARIA_PCM_FRAME_BYTES);
  for (let i = 0; i < audio.AUDIO_RATE; ++i) {
    const t = i / audio.AUDIO_RATE;
    const envelope = 0.2 + 0.8 * Math.exp(-((t * 2) % 1) * 8);
    for (let channel = 0; channel < ariacast.ARIA_PCM_CHANNELS; ++channel) {
      const phase = channel ? 0.4 : 0;
      const sample =
        envelope *
        (0.5 * Math.sin(2 * Math.PI * 56 * t + phase) +
          0.25 * Math.sin(2 * Math.PI * 440 * t - phase) +
          0.15 * Math.sin(2 * Math.PI * 3520 * t));
      pcm.writeInt16LE(
        Math.trunc(sample * audio.AUDIO_PCM16_SCALE),
        i * ariacast.ARIA_PCM_FRAME_BYTES +
          channel * ariacast.ARIA_PCM_SAMPLE_BYTES,
      );
    }
  }
  return pcm;
}

/** Benchmark-only signal and phase reset; transport is shared with the live sender. */
export function streamAudio(
  address,
  onError,
  port = ariacast.ARIA_STREAM_PORT,
) {
  const pcm = generatedPCM();
  let position = 0;
  const stream = streamPCM(address, onError, {
    port,
    retryConnection: true,
    readPacket() {
      const packet = pcm.subarray(position, position + ariacast.ARIA_PCM_BYTES);
      position = (position + ariacast.ARIA_PCM_BYTES) % pcm.length;
      return packet;
    },
  });
  return {
    resetPhase() {
      position = 0;
    },
    get packets() {
      return stream.packets;
    },
    close() {
      stream.close();
    },
  };
}
