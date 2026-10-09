# MilkDrop tools

<!-- BEGIN TOC -->

## Contents

- [Preset compilation](#preset-compilation)
  - [Compiler ownership](#compiler-ownership)
- [Run a benchmark](#run-a-benchmark)
- [Test conditions](#test-conditions)
- [Reports and interpretation](#reports-and-interpretation)
  - [Recorded in both full and quick runs](#recorded-in-both-full-and-quick-runs)
  - [Additional measurements in quick runs](#additional-measurements-in-quick-runs)
- [Benchmark eligibility](#benchmark-eligibility)
  - [Playback frame-rate eligibility](#playback-frame-rate-eligibility)
- [Quick selection](#quick-selection)

<!-- END TOC -->

Run Make commands from the repository root in the development container.

## Preset compilation

App and test builds compile presets automatically. To build the release app:

```sh
make
```

Preset compilation creates `generated/preset-report.json` inside the build
directory: `build/stroom/generated/preset-report.json` after the default `make`
build. It lists preset inclusion,
source hashes and the first rejection reason. A preset can have more than one
unsupported feature.
Generated C and headers are build products, not editable sources.

[`audit_presets.mjs`](audit_presets.mjs) inventories fields and syntax. An inventory
does not establish renderer compatibility; compilation and console testing are
separate checks.

### Compiler ownership

The compiler owns equation parsing, variable layouts, object limits and expression
optimizations. It generates the layout definitions needed by C rather than asking
the runtime to maintain matching numbers independently.

Preset fields retain their first definition when repeated. Numeric fields read a
decimal prefix and fall back to their field default when no number is present,
matching the original loader for malformed entries such as `rot=-`. Explicit
NaN/infinity, overflow and unsupported value ranges remain errors. These rules
apply to preset fields; malformed equation code is still rejected. Numbered equation
fragments are joined without inserting whitespace, after removing each fragment’s
comments, as in the original loader. Each block starts at number 1 and stops at
the first missing number; an existing empty fragment does not stop loading.
Existing whitespace is preserved.

Out-of-range echo zoom is accepted only when echo alpha is zero and no equation
writes it, so the disabled effect cannot become visible.

Optimizations must preserve observable state and random-number ordering.
Custom waves compile a complete point loop per wave, using the shared
[milk_wave_points.h](../../src/common/milkdrop/milk_wave_points.h) template. Sample
conditioning stays in the runtime; point inputs, equations and vertex storage
are visible together to the C compiler. Point counts, state writes and RNG order
must stay unchanged. Specialization increases generated code size.

Differential tests compare generated expressions and complete wave loops with
their reference behavior.
Keep optimization details and limits with the implementation and tests.

## Run a benchmark

From the repository root, replace `YOUR_PS2_IP` with your console's IP address.
Eject any audio CD, configure networking in the launcher, and boot PS2Link.
Run the full compatible library:

```sh
make benchmark PS2_IP=YOUR_PS2_IP
```

For the [quick nine-preset comparison](#quick-selection):

```sh
make benchmark-quick PS2_IP=YOUR_PS2_IP
```

The final image stays onscreen after completion.

## Test conditions

The benchmark sends generated stereo audio through the shared
[AriaCast transport](../aria/README.md) to the speakers and renders fullscreen
with level meters. It ignores `STROOM.DAT` and bypasses playback's frame-rate cap
and preset eligibility filters.

Each preset uses five seconds, including one second of excluded warmup, with a
fixed seed and fresh feedback. Presets with
brighten, darken or solarize enabled by default trigger a GPU pixel check before
measurement; see [colour-curve validation](../../src/common/ui/visualizer/color_curve.md) for more details.

Both full and quick benchmarks use diagnostic builds and real network/audio
workers.

## Reports and interpretation

Each run saves `results.json`, `results.csv`, and `console.log` in a timestamped
folder on your computer:

- Full: `build/benchmark-full/benchmark/<timestamp>/`
- Quick: `build/benchmark-quick/benchmark/<timestamp>/`

Interrupted runs keep partial results. JSON records the run settings, display
timing, preset paths and source hashes alongside the measurements.

### Recorded in both full and quick runs

Times use microseconds (`us`). Measurements exclude warmup, except for
`first_work_us`. **Work** excludes waiting for display refresh; **interval**
includes that wait.

| Field                                                                   | Meaning                                                                                      |
| ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `steady_fps`                                                            | Presented frames per second after warmup; used for playback eligibility                      |
| `work_avg_us`                                                           | Average work time per measured frame                                                         |
| `work_p95_us`                                                           | Work time covering 95% of measured frames, conservatively estimated using 1 ms buckets       |
| `work_max_us`                                                           | Longest work time of any measured frame                                                      |
| `interval_avg_us`, `interval_p95_us`, `interval_max_us`                 | Average, 95th percentile and longest whole-frame duration, including display refresh waiting |
| `preparation_avg_us`, `update_avg_us`, `render_avg_us`, `submit_avg_us` | Average time in each main stage; together they make up frame work                            |
| `first_work_us`                                                         | Work time of the first frame, before warmup                                                  |
| `over_budget`                                                           | Number of measured frames whose work exceeds the nominal 30 FPS time budget                  |

Submission includes sending queued drawing commands and waiting for GPU
completion. All timings include time when other workers interrupt the measured
work.

The report's `classification` is **slow** if average work exceeds the budget,
**borderline** if only P95 exceeds it, and **okay** if both fit. Playback builds
require both an **okay** classification and the baseline measured FPS;
the runtime filter then uses [measured FPS](#playback-frame-rate-eligibility).

### Additional measurements in quick runs

Quick also records average times for smaller steps inside update and rendering.
Each value is microseconds per measured frame, excluding warmup:

| Field                         | Meaning                                                     |
| ----------------------------- | ----------------------------------------------------------- |
| `audio_analysis_avg_us`       | Audio analysis before preset evaluation                     |
| `preset_equations_avg_us`     | Preset frame and mesh equations                             |
| `custom_wave_update_avg_us`   | Complete custom-wave update, including the wave steps below |
| `custom_shape_update_avg_us`  | Custom-shape equations and state updates                    |
| `mesh_avg_us`                 | Feedback mesh calculations                                  |
| `feedback_geometry_avg_us`    | Feedback geometry, excluding command construction           |
| `feedback_commands_avg_us`    | Feedback drawing-command construction                       |
| `wave_geometry_avg_us`        | Wave geometry, excluding command construction               |
| `shape_geometry_avg_us`       | Shape geometry, excluding command construction              |
| `draw_commands_avg_us`        | Wave and shape drawing-command construction                 |
| `presentation_avg_us`         | Visualization effects, excluding display refresh waiting    |
| `wave_frame_equations_avg_us` | Custom-wave per-frame equations                             |
| `wave_samples_avg_us`         | Custom-wave sample conditioning                             |
| `wave_point_inputs_avg_us`    | Preparing inputs for each custom-wave point                 |
| `wave_point_equations_avg_us` | Custom-wave per-point equations                             |
| `wave_vertex_store_avg_us`    | Storing custom-wave output vertices                         |

These timings are parts of the main stages, so do not add them to the main-stage
totals. The five `wave_*` update steps are already included in
`custom_wave_update_avg_us`.

**Full reports contain these extra fields as zero because they are not measured.**
The JSON settings `render_profile`, `detail_profile` and `wave_profile` are all
`0` for full runs and `1` for quick runs.

Quick's extra timing calls add overhead. Use full runs for playback eligibility
and quick runs to locate costs. Compare performance between runs of the same
type, with matching settings and the same presets.

## Benchmark eligibility

The latest JSON report in [`benchmarks/`](../../src/common/milkdrop/benchmarks) supplies all preset selection
and measured FPS data. Timestamped filenames are sorted newest first; file
modification times do not affect selection. Builds fail if the directory contains
no report or the latest report is incomplete, profiled, malformed or measured
under a different display profile.

Normal playback builds include presets classified `okay` meeting the 29.97 FPS
baseline cutoff. Quick builds include
presets marked `quick: true`, independently of their classification. Full
benchmarks compile every compatible source preset, including unmeasured or
changed presets. Missing, unsupported or changed-source presets selected for
playback or quick benchmarking fail compilation: measurements must match the
preset's source path and SHA-256.

A successful full sweep atomically archives its report into the configured
benchmark directory. The next build discovers it automatically. Quick, interrupted,
incomplete, changed-source, mismatched-display or detailed-profile runs do not
update the archived baseline.

To use another pack, set the CMake options `PRESET_ROOT` to its source directory
and `MILKDROP_BENCHMARK_DIR` to its report directory. The report directory must
contain a qualifying full report, including for a full benchmark build. App and
host test builds accept the same paths; cached values remain until changed or
the build directory is cleaned.

### Playback frame-rate eligibility

The runtime frame-rate filter uses `steady_fps`, including presentation waiting.
The cutoffs come from the shared [display timing](../../shared/contracts/display.json)
and [MilkDrop contracts](../../shared/contracts/milkdrop.json): **29.97 FPS** for
nominal 30 FPS and **59.94 FPS** for nominal 60 FPS on the 60000/1001 Hz display.
The build derives the supported [frame-rate settings](../../src/common/ui/README.md#frame-rate)
from each preset's measured FPS. Work classification and the baseline FPS cutoff determine build inclusion;
measured FPS determines eligibility for a selected runtime frame-rate filter.

## Quick selection

Each complete, unprofiled full benchmark records `quick: true` for the three
slowest, three middle and three fastest presets ranked by `work_avg_us`, and
`quick: false` for the rest. Mean work time distinguishes fast presets that reach
the display's FPS ceiling. Ties are ordered by preset path; for an even library
size, the middle group centers on the upper middle index.

`quick` is `null` for incomplete or profiled runs and full sweeps of fewer than
nine presets. Smaller full sweeps can drive playback but cannot supply a quick
benchmark. Keep the source report fixed during an optimization comparison.
The nine presets take 45 seconds of measurement, plus startup and validation.
