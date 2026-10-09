#pragma once

#define PS2_GS_OPAQUE_ALPHA 0x80000000u
#define PS2_GS_ALPHA_MASK   0xff000000u

/* GS register addressing units and GIF tag fields, usable by host packet tests. */
#define PS2_GS_FRAME_PAGE_BYTES    8192
#define PS2_GS_BUFFER_WIDTH_PIXELS 64
#define PS2_GIF_EOP_SHIFT          15
#define PS2_GIF_FORMAT_SHIFT       58
#define PS2_GIF_NREG_SHIFT         60
#define PS2_GIF_REGLIST            1
#define PS2_GIF_AD                 0xe
#define PS2_GIF_PRIM               0
#define PS2_GIF_RGBAQ              1
#define PS2_GIF_UV                 3
#define PS2_GIF_XYZ2               5
#define PS2_GIF_REGISTER_BITS      4
