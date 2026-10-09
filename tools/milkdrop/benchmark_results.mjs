import { markQuickPresets } from "./select_quick_presets.mjs";
import { milkdrop, benchmarkProfileFields } from "../contracts/load.mjs";

const profileKeys = (group) =>
  benchmarkProfileFields
    .filter((field) => field.group === group)
    .map((field) => field.name + "_avg_us");
const waveDetails = profileKeys("wave");
const updateDetails = profileKeys("update");
const renderDetails = profileKeys("render");

/** Collect one run, preserving identities from its build's compiler report. */
export class BenchmarkResults {
  constructor(entries) {
    this.entries = entries.filter((entry) => entry.status === "INCLUDED");
    this.state = {
      status: "waiting",
      configuration: null,
      current: null,
      results: [],
    };
  }

  consume(event) {
    if (event.event === "color_curve_validation") {
      if (
        !Number.isInteger(event.mode) ||
        event.mode <= 0 ||
        event.mode > milkdrop.MILKDROP_COLOR_CURVE_MASK ||
        this.state.configuration ||
        !Number.isSafeInteger(event.pixels) ||
        event.pixels <= 0 ||
        !Number.isSafeInteger(event.mismatches) ||
        event.mismatches < 0 ||
        event.mismatches > event.pixels ||
        typeof event.readback !== "boolean"
      )
        throw Error("Invalid color curve validation");
      this.state.color_curve_validations ??= [];
      this.state.color_curve_validations.push(event);
    } else if (event.event === "start") {
      if (
        this.state.configuration ||
        event.version !== 2 ||
        event.total !== this.entries.length ||
        event.audio !== "generated-ariacast" ||
        !Number.isSafeInteger(event.duration_us) ||
        event.duration_us <= 0 ||
        !Number.isSafeInteger(event.warmup_us) ||
        event.warmup_us < 0 ||
        event.warmup_us >= event.duration_us ||
        !Number.isSafeInteger(event.budget_us) ||
        event.budget_us <= 0
      ) {
        throw Error(
          "Benchmark configuration does not match the compiled preset library",
        );
      }
      this.state.configuration = event;
      this.state.status = "running";
    } else if (event.event === "begin") {
      this.checkIndex(event.index);
      if (
        this.state.current !== null ||
        event.index !== this.state.results.length
      )
        throw Error("Preset begin event out of sequence");
      this.state.current = event.index;
    } else if (event.event === "result") {
      this.checkIndex(event.index);
      if (this.state.results.some((result) => result.index === event.index)) {
        throw Error(`Duplicate result for preset ${event.index}`);
      }
      if (event.index !== this.state.current)
        throw Error("Timing result has no matching preset begin event");
      const numeric = [
        "elapsed_us",
        "frames",
        "warmup_frames",
        "first_work_us",
        "preparation_avg_us",
        "update_avg_us",
        "render_avg_us",
        "submit_avg_us",
        "samples",
        "work_avg_us",
        "work_p95_us",
        "work_max_us",
        "interval_avg_us",
        "interval_p95_us",
        "interval_max_us",
        "steady_fps",
        "over_budget",
      ];
      if (
        numeric.some((key) => !Number.isFinite(event[key]) || event[key] < 0) ||
        event.elapsed_us < this.state.configuration.duration_us ||
        !event.samples ||
        ["frames", "warmup_frames", "samples", "over_budget"].some(
          (key) => !Number.isSafeInteger(event[key]),
        ) ||
        event.frames !== event.samples + event.warmup_frames ||
        event.over_budget > event.samples
      ) {
        throw Error(`Invalid timing result for preset ${event.index}`);
      }
      if (this.state.configuration.render_profile === 1) {
        const parts = [
          "mesh_avg_us",
          "feedback_geometry_avg_us",
          "feedback_commands_avg_us",
        ];
        if (
          parts.some((key) => !Number.isFinite(event[key]) || event[key] < 0) ||
          parts.reduce((sum, key) => sum + event[key], 0) >
            event.render_avg_us + 1
        ) {
          throw Error("Invalid render profile");
        }
      }
      if (this.state.configuration.detail_profile === 1) {
        const update = updateDetails.reduce((sum, key) => sum + event[key], 0);
        const render = [
          ...renderDetails,
          "mesh_avg_us",
          "feedback_geometry_avg_us",
          "feedback_commands_avg_us",
        ].reduce((sum, key) => sum + event[key], 0);
        if (
          [...updateDetails, ...renderDetails].some(
            (key) => !Number.isFinite(event[key]) || event[key] < 0,
          ) ||
          update > event.update_avg_us + 1 ||
          render > event.render_avg_us + 1
        ) {
          throw Error("Invalid detailed profile");
        }
      }
      if (this.state.configuration.wave_profile === 1) {
        if (
          waveDetails.some(
            (key) => !Number.isFinite(event[key]) || event[key] < 0,
          ) ||
          waveDetails.reduce((sum, key) => sum + event[key], 0) >
            event.custom_wave_update_avg_us + 1
        ) {
          throw Error("Invalid wave profile");
        }
      }
      const budget = this.state.configuration.budget_us;
      const classification =
        event.work_avg_us > budget
          ? "slow"
          : event.work_p95_us > budget
            ? "borderline"
            : "okay";
      this.state.results.push({
        ...event,
        file: this.entries[event.index].file,
        sha256: this.entries[event.index].sha256,
        classification,
        quick: null,
      });
      this.state.current = null;
    } else if (event.event === "complete") {
      if (
        this.state.status !== "running" ||
        this.state.current !== null ||
        !this.state.configuration ||
        event.total !== this.entries.length ||
        this.state.results.length !== this.entries.length
      ) {
        this.state.status = "incomplete";
        throw Error(
          "Console finished but not every preset result was received",
        );
      }
      this.state.status = "complete";
      this.state.current = null;
      markQuickPresets(this.state);
    } else if (event.event === "aborted" || event.event === "error") {
      this.state.status = event.event;
      throw Error(
        event.message || `Benchmark aborted at preset ${event.index}`,
      );
    } else {
      throw Error("Unknown benchmark event");
    }
  }

  checkIndex(index) {
    if (
      this.state.status !== "running" ||
      !Number.isInteger(index) ||
      index < 0 ||
      index >= this.entries.length
    ) {
      throw Error("Preset index outside the active benchmark library");
    }
  }

  csv() {
    const keys = [
      "index",
      "file",
      "classification",
      "quick",
      "steady_fps",
      "work_avg_us",
      "work_p95_us",
      "work_max_us",
      "update_avg_us",
      "render_avg_us",
      "mesh_avg_us",
      "feedback_geometry_avg_us",
      "feedback_commands_avg_us",
      "submit_avg_us",
      "preparation_avg_us",
      "interval_avg_us",
      "interval_p95_us",
      "interval_max_us",
      "first_work_us",
      "frames",
      "samples",
      "over_budget",
      ...waveDetails,
      ...updateDetails,
      ...renderDetails,
      "sha256",
    ];
    const cell = (value) => `"${String(value ?? "").replaceAll('"', '""')}"`;
    return (
      [
        keys,
        ...this.state.results.map((result) => keys.map((key) => result[key])),
      ]
        .map((row) => row.map(cell).join(","))
        .join("\n") + "\n"
    );
  }
}
