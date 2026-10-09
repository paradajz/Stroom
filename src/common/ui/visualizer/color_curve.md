# Presentation colour curves

Classic MilkDrop darken squares each normalized RGB channel. Classic brighten
inverts, squares, and inverts again: `1 - (1 - c)²`. This follows the upstream
fixed-function renderer; the shader-based square-root approximation is a different
curve. Classic solarize multiplies by the inverse and doubles the result:
`2 * c * (1 - c)`, with rounding before doubling. The shader version uses a
different factor. The order is brighten, darken, solarize, then invert.

The GS applies these operations through seven precomputed palettes covering every
nonempty combination of the three effects. Each operation rounds to an 8-bit value.
Combined effects share one rendering pass; they do not require separate scaling passes
or another set of scratch buffers. Switching effects selects an immutable palette.

The pass downsamples the completed visualization to half width and height,
applies the selected palette, and upscales before UI overlays. Feedback and UI
retain the [display resolution](../../platform/README.md#display). This
intentionally softens the presentation effect without reducing the whole engine.

## Implementation

GS local-to-local transfers copy strips into scratch memory. Their CT32 bytes
are reinterpreted as T8 palette indices, with separate RGB passes and write
masks preserving alpha. The different within-block layouts require small
rectangles; immutable DMA packets are reused across strips.

Both scaling passes use tiled bilinear sampling to limit interpolation error.
Curves follow gamma/echo; clean feedback is preserved before presentation.
Frame equations may change any flag, and transitions select all flags from
the same preset as the other discrete presentation effects.

## Validation

Effect bits and transmitted validation modes are defined by the
[shared MilkDrop contract](../../../../shared/contracts/milkdrop.json).

Portable tests cross-check byte correspondence, all palette entries, effect
ordering, and feedback preservation. For selections with a curve enabled by
default, the benchmark uploads a test image and verifies all seven modes through
GPU readback, including palette switches. Every pixel is compared with an independent
downsample/curve/upscale reference, allowing three RGB code values of rounding
error and requiring exact alpha. Validation is outside measured frames. A mismatch
stops measurement and leaves the error screen visible; reports retain
`color_curve_validations` with the mode and error counts.
