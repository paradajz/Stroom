# Settings and desktop MilkDrop

This comparison concerns the [checked-in MilkDrop3 source](../../../../third_party/MilkDrop3),
not settings offered by every desktop release. It is a reference dependency,
not part of the PS2 build.

See the [menu guide](../README.md#menu) and
[startup configuration](../../../../README.md#startup-configuration).

## Behavioral differences

- Preset selection uses fixed, sequential or shuffled eligible presets, without
  desktop rating weights. Eligibility comes from the saved benchmark results.
- Automatic transitions wait for the configured interval and variation after a
  blend finishes. Manual and automatic changes share the same blend policy;
  they do not expose separate desktop blend-duration settings.
- The PS2 renderer has a fixed feedback mesh and video mode. There is no desktop
  quality selector, runtime shader compiler or live preset editor. See
  [preset compatibility](../../milkdrop/README.md#preset-compatibility) for supported effects.

Desktop source defaults can be overridden by its configuration file. They are
not a reliable description of a user's installation and should not be copied
into this guide as PS2 defaults. The relevant reference code is in
[plugin.cpp](../../../../third_party/MilkDrop3/code/vis_milk2/plugin.cpp) and
[milkdropfs.cpp](../../../../third_party/MilkDrop3/code/vis_milk2/milkdropfs.cpp).
