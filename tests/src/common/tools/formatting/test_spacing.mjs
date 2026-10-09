import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const formatter = fileURLToPath(
  new URL(
    "../../../../../tools/formatting/format-c-spacing.mjs",
    import.meta.url,
  ),
);
const directory = mkdtempSync(path.join(tmpdir(), "stroom-spacing-"));
const file = path.join(directory, "fixture.c");

function check(input, expected) {
  writeFileSync(file, input);
  execFileSync(process.execPath, [formatter, file]);
  assert.equal(readFileSync(file, "utf8"), expected);
  execFileSync(process.execPath, [formatter, file]);
  assert.equal(
    readFileSync(file, "utf8"),
    expected,
    "spacing must be idempotent",
  );
}

try {
  check(
    `void frame(void)
{
    uint32_t now = platform_millis();
    audio_source_poll(&audio, &source);
    cd_lookup_poll(&source);
    InputState input = platform_pad_read(pad_open);
    AppActions actions = app_update(&app, &audio, &source, input, now);
    UiScreen screen = app_screen(&app, &source);
    audio_source_set_muted(app.settings.muted);
    audio_source_apply(&source, &actions.transport);
}
`,
    `void frame(void)
{
    uint32_t now = platform_millis();

    audio_source_poll(&audio, &source);
    cd_lookup_poll(&source);

    InputState input = platform_pad_read(pad_open);
    AppActions actions = app_update(&app, &audio, &source, input, now);
    UiScreen screen = app_screen(&app, &source);

    audio_source_set_muted(app.settings.muted);
    audio_source_apply(&source, &actions.transport);
}
`,
  );

  check(
    `void frame(void)
{
    State state = {
        .text = "}; not a statement;",
        .count = 1
    };
    const unsigned long count = 2;
    void (*callback)(void) = draw;
    const char* title = "Track";
    draw(state, count, callback, title);

    state.count = 2;
    state.count += 1;
}
`,
    `void frame(void)
{
    State state = {
        .text = "}; not a statement;",
        .count = 1
    };
    const unsigned long count = 2;
    void (*callback)(void) = draw;
    const char* title = "Track";

    draw(state, count, callback, title);

    state.count = 2;
    state.count += 1;
}
`,
  );

  const untouched = `typedef struct
{
    int first;
    int second;
} Pair;

#define DECLARE() \\
    int first;   \\
    use(first);

void frame(void)
{
    struct Local
    {
        int first;
        int second;
    };

    // Attached declaration comment.
    int first;
    int second;
}
`;
  check(untouched, untouched);

  check(
    `void frame(void)
{
    if (ready == 1)
    {
        draw();
        int count = 1;
        use(count);
    }
}
`,
    `void frame(void)
{
    if (ready == 1)
    {
        draw();

        int count = 1;

        use(count);
    }
}
`,
  );
  check(
    `struct State* build(void)
{
    for (int i = 0; i < 2; ++i)
    {
        draw();
        unsigned count = i;
        use(count);
    }

    State* result = create();
    return result;
}
`,
    `struct State* build(void)
{
    for (int i = 0; i < 2; ++i)
    {
        draw();

        unsigned count = i;

        use(count);
    }

    State* result = create();

    return result;
}
`,
  );

  check(
    `void frame(void)
{
    app_artwork_prepare(&source);

    ui_frame_draw(gs,
        screen, &source, now);

    app_artwork_after_draw(&source, screen);

    ui_frame_present(gs, frame_rate, now);
    app_artwork_after_present(&source, screen, now);

    // Keep this comment attached to its call.
    report();

    uint32_t done = now;
    save(done);
}
`,
    `void frame(void)
{
    app_artwork_prepare(&source);
    ui_frame_draw(gs,
        screen, &source, now);
    app_artwork_after_draw(&source, screen);
    ui_frame_present(gs, frame_rate, now);
    app_artwork_after_present(&source, screen, now);

    // Keep this comment attached to its call.
    report();

    uint32_t done = now;

    save(done);
}
`,
  );

  check(
    `void poll(void)
{
    WaitSema(lock);
    requests.generation = published.generation;
    requests.scan_direction = requested_scan;
    requests.program_count = program_count;
    memcpy(requests.program, program_tracks, program_count * sizeof(*program_tracks));
    program_count = 0;
    requests.have_command = command_count != 0;
    SignalSema(lock);
}
`,
    `void poll(void)
{
    WaitSema(lock);

    requests.generation = published.generation;
    requests.scan_direction = requested_scan;
    requests.program_count = program_count;

    memcpy(requests.program, program_tracks, program_count * sizeof(*program_tracks));

    program_count = 0;
    requests.have_command = command_count != 0;

    SignalSema(lock);
}
`,
  );

  check(
    `int valid(void)
{
    SignalSema(lock);
    return valid;
}

int value(void)
{
    return 1;
}

void finish(void)
{
    release();
    return;
}
`,
    `int valid(void)
{
    SignalSema(lock);

    return valid;
}

int value(void)
{
    return 1;
}

void finish(void)
{
    release();

    return;
}
`,
  );

  check(
    `int result(void)
{
    if (failed)
    {
        release();
        return 0;
    }

    release();
    return calculate(
        "a;b",
        result);
}
`,
    `int result(void)
{
    if (failed)
    {
        release();
        return 0;
    }

    release();

    return calculate(
        "a;b",
        result);
}
`,
  );

  check(
    `int app_artwork_open(void)
{
#if STROOM_DIAGNOSTICS
    presented_at = 0;
    have_presented = cover_frame = 0;

    ui_artwork_set_observer(network_artwork_observe);
#endif
    return ui_artwork_open();
}
`,
    `int app_artwork_open(void)
{
#if STROOM_DIAGNOSTICS
    presented_at = 0;
    have_presented = cover_frame = 0;

    ui_artwork_set_observer(network_artwork_observe);
#endif

    return ui_artwork_open();
}
`,
  );

  console.log(
    "PASS: declaration groups, initializers, scopes, macros and idempotence.",
  );
} finally {
  rmSync(directory, { recursive: true, force: true });
}
