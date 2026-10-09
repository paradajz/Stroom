# Host tools

<!-- BEGIN TOC -->

## Contents

- [Guides](#guides)
- [Formatting](#formatting)
- [C lint](#c-lint)

<!-- END TOC -->

Start with the [everyday build and audio commands](../README.md). This index
links to detailed tool options and developer references. Run commands from the
repository root in the environment shown below.

## Guides

| Task                                             | Run on                             | Guide                                                                           |
| ------------------------------------------------ | ---------------------------------- | ------------------------------------------------------------------------------- |
| Install Stroom or PS2Link on a memory card       | Build in container; install on PS2 | [Console setup](../setup-manual.md)                                             |
| Build the app, SDK or launcher                   | Development container              | [Build tools](sdk/README.md)                                                    |
| Listen to PS2 CD audio                           | Listening computer                 | [CD listener](cd/listener/README.md#stream-cd-audio-to-this-computer)           |
| Install the CD listener as a user service        | Listening computer                 | [Listener service](cd/listener/README.md#install-as-a-background-service-linux) |
| Recognize CDs and fetch cover art                | Metadata-service computer          | [CD recognition](cd/recognition/README.md#run-the-recognition-service)          |
| Send audio or silent visualizer input to the PS2 | Sending computer                   | [AriaCast sender](aria/README.md)                                               |
| Query or capture an incoming-audio dropout       | Development container              | [Diagnostics](diagnostic/README.md)                                             |
| Compile presets or benchmark on the PS2          | Development container              | [MilkDrop](milkdrop/README.md)                                                  |
| Change shared wire formats and constants         | Development container              | [Contracts](../shared/README.md)                                                |

## Formatting

```sh
make format
```

This modifies project files in place using clang-format, the repository's C
spacing pass, and Prettier. Formatting rules live in their configuration files;
review the resulting diff before committing.

README tables of contents are generated from section headings by
[readme-toc.mjs](formatting/readme-toc.mjs). `make format` updates them automatically;
edit the headings rather than the generated navigation. READMEs without sections
have no TOC. The TOC follows the title, or precedes the first section when a
README uses a logo instead of a title.

The formatter skips dependency directories and the build paths supplied by Make.
See [format.mjs](formatting/format.mjs) for file selection and exclusions.

Keep C file-level declarations in this order: includes, defines, static data and
forward declarations, then function bodies. Put required type declarations before
the static data that uses them. Use `#pragma once` for headers and preserve
conditional build scopes.

Document public functions at their header declarations and private functions at
their definitions with Doxygen comments. Describe parameters, return values,
ownership and asynchronous completion where they affect callers. Keep a blank
line between a function declaration and the next function's documentation;
the spacing pass preserves this separation. Test functions and substitutes also
need Doxygen descriptions of their purpose.

Within function bodies, separate consecutive variable declarations and assignments
from surrounding statements with a blank line. Keep each group together and
avoid padding next to block braces or conditional directives.
Use one assignment per statement; do not chain assignments.
Group consecutive standalone function calls without blank lines between them;
comments and control blocks retain their separation.
Separate a function's final return from preceding statements with a blank line,
while keeping return-only bodies compact.

## C lint

```sh
make lint
```

This checks the project’s C and C++ code with clang-tidy. JavaScript tools are not checked.

Rules live in [.clang-tidy](../.clang-tidy) and directory-specific overrides.
`CLANG_TIDY` selects the executable; the `JOBS` environment variable controls
concurrent checks. Enabled warnings and parsing errors fail the command.

Clang uses an approximation of the PS2 target for analysis. A passing lint run
does not replace building with the real PS2 compiler or testing on the console.
