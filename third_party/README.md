# Third-party dependencies and notices

- presets-milkdrop-original is a Git submodule of
  https://github.com/projectM-visualizer/presets-milkdrop-original.git, pinned
  to e03b83e3338d8f1ed6cbcf908c719f249ef24288. See the
  [MilkDrop build guide](../tools/milkdrop/README.md#benchmark-eligibility) for preset
  selection. Preserve upstream names and author credits.
- MilkDrop3 is a reference-only Git submodule of
  https://github.com/milkdrop2077/MilkDrop3.git, pinned to
  6b39088f789441e18cc1665519ee7ffe439cece5. It is not linked into the PS2 build.
- Unity is a test-only Git submodule of
  https://github.com/ThrowTheSwitch/Unity.git, pinned to v2.7.0
  (b6763fbd9cedfacaa89e2ad9fd00d615a234e355). Its MIT license is in
  Unity/LICENSE.txt. It is not linked into the PS2 application.
- MilkDrop-LICENSE.txt and BeatDrop-LICENSE.txt cover adapted reference code.
- PS2DEV-LICENSE.txt contains the workspace scaffold's license notice.

Run `git submodule update --init --recursive` from the repository after cloning.
The pinned MilkDrop3 source is available for inspection.

The [PS2SDK socket RPC patch](../patches/README.md#ps2sdk) is applied to pinned
sources from the `ps2sdk` submodule during the build. The sources retain
AFL-2.0; the patch directory documents the changes and includes the license.

PS2SDK and ps2sdk-ports are pinned submodules built by
the [SDK tooling](../tools/sdk/README.md). Their upstream scripts own dependency
downloads and builds, including gsKit, zlib, PNG and JPEG. No inherited SDK or image-library binaries
are used.

JPEG retains its [IJG notice](libjpeg-license.txt); PNG and zlib retain their
[libpng](libpng-license.txt) and [zlib](zlib-license.txt) notices. gsKit retains
its AFL-2.0 license in its source tree.

The optional
[PS2Link build](../tools/sdk/README.md#build-the-launcher) uses the `ps2link` submodule, pinned
to `0c6138c5553760423070d1797ac475c4d98a06e6`. Its license and notices remain in
that source tree. It is a separate launcher, not part of the app ELF.
