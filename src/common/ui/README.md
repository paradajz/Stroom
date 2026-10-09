# User interface

<!-- BEGIN TOC -->

## Contents

- [Shared controls](#shared-controls)
- [CD player](#cd-player)
  - [Playback and programs](#playback-and-programs)
- [Network player](#network-player)
  - [Listening](#listening)
- [Menu](#menu)
  - [Visualiser](#visualiser)
    - [Frame rate](#frame-rate)
- [Fullscreen visualizer](#fullscreen-visualizer)
- [Metadata and artwork](#metadata-and-artwork)

<!-- END TOC -->

MilkDrop runs behind the player and waiting screens. Menus do not pause the
visualization. The level-meter setting applies in both playback views.

## Shared controls

- **Start:** play/pause CD audio, including in menus, program editing and fullscreen.
- **Select:** open or close the menu. Its **Exit** item closes the menu, not the app.
- **R2:** switch between player panels and fullscreen during playback, closing an
  open menu. It is disabled on the waiting screen and during program editing.
- **L2:** mute audible playback without pausing audio transport or the visualizer.

## CD player

Outside menus and program editing, **Triangle** stops, tapping **L1/R1** skips
tracks, and holding L1/R1 scans. These controls also apply in fullscreen.

Until tracks are available, the header shows **CD / LOADING**. Failures show
**CD / ERROR**; details stay in diagnostics. **Start** retries preparation. See
[detection and recovery](../audio/README.md#detection-and-recovery).

Optional [CD recognition](../../../tools/cd/README.md) supplies labels and cover
art independently of playback.

### Playback and programs

The Playback submenu offers Continue, Shuffle, Repeat current, Repeat all and
Program modes. It also contains program editing, elapsed/remaining time and
sound settings. Remaining time is prefixed with a minus sign.

In the program grid, use directions to navigate and **Cross** to select or remove
tracks. **Square** confirms a nonempty program and activates Program mode.
**Triangle** or **Select** cancels. Both confirmation and cancellation return to
the Playback menu.

A confirmed program remains available when you temporarily select another mode.
Selecting Program again starts from its first programmed track while preserving
play/pause state. Cancelling edits keeps the previous program; replacing the
disc clears it. For long discs, `...` advances the track window.

## Network player

Playback is controlled by the casting device. Network playback has no track-time
or progress display. For audible streaming, the Playback submenu offers only Sound.

### Listening

Listening sessions drive MilkDrop without PS2 sound. They hide the Playback submenu
and ignore L2. The lower panel shows a supplied device name, without track
metadata or artwork.

A new listening session hides any visible panels after five seconds. An open
menu delays that switch; R2 or changing Display cancels the automatic switch.
The panels can then be restored manually.

## Menu

**Up/Down** chooses a row, **Cross** opens a submenu, and **Triangle** goes back.

See the [settings comparison](settings/reference.md).

### Visualiser

Display is the first setting and selects **Panels / Full screen**. It changes
the playback layout while keeping the menu open. It does not change the
[physical video mode](../platform/README.md#display).

Other settings control preset selection, preset-name visibility, level meters,
change timing, variation and music-triggered hard cuts. Use Left/Right or Cross
to change a setting. See [startup configuration](../../../README.md#startup-configuration).

#### Frame rate

Frame rate selects **30 / 60 FPS** and limits playback to approved presets
benchmarked as fast enough for the selected setting. Switching rates ends any
blend and replaces the current preset if it does not qualify. A rate with no
qualifying presets cannot be selected.

This is not a live performance test. A temporary slowdown does not remove or skip
the preset. Faster presets wait for presentation; animation still uses elapsed
time and audio continues independently. See
[display timing](../platform/README.md#configuration-and-timing)
and [benchmark eligibility](../../../tools/milkdrop/README.md#playback-frame-rate-eligibility).

## Fullscreen visualizer

In fullscreen, outside menus:

- **Left/Right:** previous/next preset.
- **Network only:** Triangle toggles feedback, L1 switches fixed/shuffle mode,
  and R1 advances the preset.

## Metadata and artwork

CD and audible network playback share title, artist, album and cover presentation. Missing labels
remain blank. Long labels scroll independently. Supported Latin accents and
punctuation are folded for the uppercase bitmap font; unsupported characters
appear as `?`. This changes display text only, preserving original metadata.

When labels are present without a usable image, a generic cover placeholder is
shown. With neither labels nor artwork, the cover area stays empty.

See the [artwork pipeline](../audio/artwork/README.md).
