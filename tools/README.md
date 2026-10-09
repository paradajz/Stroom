# Host tools

<!-- BEGIN TOC -->

## Contents

- [Guides](#guides)
- [Formatting](#formatting)
- [C lint](#c-lint)

<!-- END TOC -->

Run commands from the repository root. Build and development tools use the
development container; the audio sender and CD service run on the computer
providing audio or metadata.

## Guides

- [Build tools](sdk/README.md)
- [Contracts](../shared/README.md)
- [CD recognition](cd/README.md)
- [AriaCast sender](aria/README.md)
- [Diagnostics](diagnostic/README.md)
- [MilkDrop](milkdrop/README.md)

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

Within function bodies, separate consecutive variable declarations and assignments
from surrounding statements with a blank line. Keep each group together and
avoid padding next to block braces or conditional directives.
Group consecutive standalone function calls without blank lines between them;
comments and control blocks retain their separation.
Separate a function's final return from preceding statements with a blank line,
while keeping return-only bodies compact.

## C lint

```sh
make lint
```

This checks the project’s C code with clang-tidy. JavaScript tools are not checked.

Rules live in [.clang-tidy](../.clang-tidy) and directory-specific overrides.
`CLANG_TIDY` selects the executable; the `JOBS` environment variable controls
concurrent checks. Enabled warnings and parsing errors fail the command.

Clang uses an approximation of the PS2 target for analysis. A passing lint run
does not replace building with the real PS2 compiler or testing on the console.
