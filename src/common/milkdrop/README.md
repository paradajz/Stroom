# MilkDrop

<!-- BEGIN TOC -->

## Contents

- [Preset compatibility](#preset-compatibility)
- [Audio compatibility](#audio-compatibility)
  - [Deliberate differences](#deliberate-differences)
  - [Shared behavior](#shared-behavior)
- [Rendering](#rendering)
- [Optimizations](#optimizations)
  - [Retained changes](#retained-changes)
  - [Rejected experiments](#rejected-experiments)

<!-- END TOC -->

The PS2 runs preset equations [compiled into native C](../../../tools/milkdrop/README.md#preset-compilation)
rather than parsing `.milk` files at runtime.
See the [UI guide](../ui/README.md).

## Preset compatibility

The reference library is the pinned [original preset pack](../../../third_party/presets-milkdrop-original).
Classic brighten, darken and solarize are supported through
[presentation colour curves](../ui/visualizer/color_curve.md).
Programmable warp/composite shaders and red/blue stereo are unsupported.
The compiler also rejects malformed equations, unsupported value ranges and
presets exceeding its equation or shape-instance budgets.

Compiler acceptance does not establish console performance or visual suitability.
Validate rendering changes with GPU readback and review presets
visually on the console before relying on new benchmark measurements.

## Audio compatibility

Both audio sources feed the same analysis path.

The PS2 engine adapts audio analysis from the [checked-in MilkDrop3 source](../../../third_party/MilkDrop3).
This describes current behavior, not complete desktop equivalence. Renderer and
shader compatibility are covered by the [preset compatibility](#preset-compatibility).

### Deliberate differences

**Time follows measured frame intervals.** Animation, preset equations and audio
envelopes use the same visible elapsed time. The desktop reference
smooths FPS and advances its clock from that estimate. Uneven frame pacing can
therefore affect motion and envelope trajectories differently even where steady
rate formulas agree. Source changes reset analysis without resetting visual time.

**Input retains PCM precision.** The PS2 resamples its output-rate PCM history to
the analysis rate using linear interpolation. It retains fractional values
instead of quantizing to the desktop adapter's signed-byte input. Quiet signals,
fine waveform detail and high-frequency response can differ. Linear interpolation
is not a band-limited converter.

**History can be reused between arrivals.** Successive visualization steps can
analyze the same PCM history until its freshness timeout expires. The reference
capture adapter returns silence after its available buffer has been consumed.
Brief input gaps can consequently hold PS2 waveform levels instead of immediately
zeroing them. Playback resets clear the history.

**Unsafe and nonfinite inputs are bounded.** Custom-wave counts, separation,
smoothing and sample indices are clamped. Geometry and colors are sanitized;
the classic logarithmic spectrum waveform has a finite floor at silence.
Presets relying on out-of-range reference behavior need not look identical.

**Equations use floats.** The reference uses double-valued equation inputs.
Fast math and rounding differences can accumulate or affect threshold decisions.
These are compatibility limits, not reasons to change audio gain globally.

### Shared behavior

- Preset bass/mid/treble inputs use the custom left-channel spectrum, not the
  player's stereo RMS meters. Right-only audio need not drive those inputs.
- Relative and attenuated bands divide their levels by a slow running average,
  with a neutral fallback near silence. Initial averaging is frame-count based,
  so its duration in seconds varies with rendering FPS.
- Custom-wave stereo spectra use damped input before waveform alignment. The
  separate custom left spectrum uses aligned input. Stereo transforms are skipped
  when neither the current preset nor its transition partner needs them.
- Custom-wave scaling includes both preset and object scales. Classic waveform
  mode 3 uses raw treble for opacity rather than a normalized band value.

The implementation lives in [audio analysis](milk_audio.c) and
[custom objects](milk_objects.c). Reference audio handling is in
[audiobuf.cpp](../../../third_party/MilkDrop3/code/audio/audiobuf.cpp) and
[milkdropfs.cpp](../../../third_party/MilkDrop3/code/vis_milk2/milkdropfs.cpp).
[Audio tests](../../../tests/src/common/milkdrop/test_milk_audio.c) check numerical behavior
and input wiring; they do not establish perceptual equivalence on hardware.

## Rendering

The [scene renderer](../ui/visualizer/scene.c) draws the engine's geometry and
feedback through the PS2 graphics adapter. It saves the raw image before
presentation effects and UI overlays, so the next feedback step samples a clean
copy. This keeps text and controls out of the image history and prevents displayed
echo effects from becoming the next raw input.

Rendering optimizations preserve geometry, draw order and graphics state.
Batching adjacent primitives reduces repeated command setup; writing vertices
directly into the graphics queue avoids an intermediate copy. Packet layouts,
queue ownership and flush requirements live beside the renderer implementation
and its tests.

## Optimizations

The following experiments reduced rendering costs or tested possible tradeoffs
on the PS2.

### Retained changes

| Change                                                                       | Outcome                                                                                                                                         |
| ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Precompute feedback-grid radius and angle                                    | Removes repeated fixed-coordinate calculations without changing the grid.                                                                       |
| Batch feedback triangles and thick-wave commands                             | Reduces repeated command setup; retained after geometry/packet checks and console comparisons.                                                  |
| Write primitive batches directly into the graphics queue                     | Removes the intermediate copy; measured improvement in expensive presets.                                                                       |
| Skip unused custom-wave stereo FFTs                                          | Avoids two transforms when neither active preset needs them.                                                                                    |
| Precompute FFT bit-reversal indices and reduce waveform-alignment scratch    | Retained CPU/memory cleanup; no isolated speedup claimed here.                                                                                  |
| Inline equation helpers and simplify redundant modulo conversions            | Compiler can optimize through calls; substantial equation savings in Digi and Robotopia for helper inlining.                                    |
| Reuse point-local trig, pair sine/cosine, cache invariant trig and decisions | Benefits depend on the preset; avoids repeated work while preserving state and random-number ordering.                                          |
| Guard disabled Boolean-multiplied trig terms                                 | Avoids unnecessary calculations where compiler analysis proves it safe.                                                                         |
| Prepare shared shape values once                                             | Reduced shape work without reducing corners or changing geometry.                                                                               |
| Fast paths for feedback clipping                                             | Reduced geometry work; FPS need not rise unless a refresh deadline is crossed.                                                                  |
| Generate the whole wave-point loop per preset                                | Retained in the latest baseline. Digi improved in the focused trial; Fractal Grinder and Robotopia slightly regressed. Not a universal speedup. |
| Half-resolution presentation darken                                          | Large cost reduction made more darken presets practical. Only the effect is reduced; feedback and UI remain full resolution.                    |
| Audio output notifications                                                   | Replaces unnecessary output-admission polling; keeps audio service independent of rendering.                                                    |

### Rejected experiments

| Experiment                                                            | Why it was reverted                                                                                                                   |
| --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Render the entire visualization at half resolution                    | Worse image quality, little gain; tested slow presets still missed 30 FPS.                                                            |
| Halve custom-wave point counts                                        | Faster, but still-image comparisons exposed missing detail and changed patterns.                                                      |
| Visible-wave coordinate fast path plus custom-shape triangle batching | No consistent combined gain; some command-construction costs increased. This does not prove each change is independently ineffective. |
| Custom-shape screen-clipping fast path                                | Lower shape cost but negligible total benefit and regressions elsewhere.                                                              |
