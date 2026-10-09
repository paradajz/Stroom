# Shared contracts

<!-- BEGIN TOC -->

## Contents

- [Definitions](#definitions)
- [Generation](#generation)
- [Validation](#validation)

<!-- END TOC -->

## Definitions

The JSON definitions in [contracts](contracts) describe values that must agree
between the console and host tools.

Edit these definitions rather than copying values into C or JavaScript.
Constant names use the filename's uppercase prefix, such as `ARIA_`.
Definitions include descriptions of their purpose. Action, event and artwork phase IDs are
explicit; diagnostic event field order defines the wire payload order.

Keep implementation-only settings local. Matching numbers do not necessarily
represent the same contract. The MilkDrop compiler generates its own variable
layouts and counts from its definitions.

## Generation

[`load.mjs`](../tools/contracts/load.mjs) validates definitions for JavaScript
consumers; [`generate.mjs`](../tools/contracts/generate.mjs) produces C headers
from the loader’s registry. Membership comes from the JSON
files in `shared/contracts`; there is no separate list to update. Named loader
exports remain convenient aliases for existing consumers. Generated headers are build products.

CMake generates headers and reconfigures when contract files are added, removed
or changed. Removing a definition also removes its generated header. To generate
them manually, run this from the repository root in the development container:

```sh
node tools/contracts/generate.mjs build/generated/contracts
```

Contract changes compiled into the diagnostic SDK require its explicit
[rebuild and installation workflow](../tools/sdk/README.md#rebuilds-and-output).

## Validation

Host tests check C/JavaScript agreement and regeneration. Keep independent wire
fixtures as well: deriving expected bytes from the contract under test would
hide accidental protocol changes.
