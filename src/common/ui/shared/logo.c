#include "ui/shared/logo.h"
#include "ui/shared/style.h"
#include <stdint.h>

#define LOGO_WIDTH       952.0f
#define LOGO_HEIGHT      264.0f
#define LOGO_POINT_SCALE 8.0f
#define LOGO_WAVE_SCALE  4096.0f

typedef struct
{
    int16_t x, y;
} LogoPoint;

typedef struct
{
    uint16_t a, b, c;
} LogoTriangle;

#include "ui/shared/logo_mesh.h"

void ui_logo_draw(GSGLOBAL* gs, float center_x, float center_y, float width)
{
    static const unsigned char colors[][3] = {
        { 90, 38, 129 },
        { 108, 43, 147 },
        { 138, 52, 166 },
        { 147, 74, 215 },
        { 130, 87, 221 },
        { 103, 102, 224 },
        { 78, 117, 220 },
        { 64, 136, 210 },
        { 61, 153, 220 },
        { 59, 170, 229 },
        { 58, 187, 237 },
        { 57, 204, 243 }
    };

    float scale = width / LOGO_WIDTH;
    float left  = center_x - width * 0.5f;
    float top   = center_y - LOGO_HEIGHT * scale * 0.5f;

    /* Draw the nested wave loops first so the lettering covers their crossings.
     * The lookup table avoids trigonometry and allocations in the frame loop. */

    for (unsigned loop = 0; loop < sizeof(colors) / sizeof(colors[0]); ++loop)
    {
        float spread     = (11.0f - loop) / 11.0f;
        float radius     = 131.0f + 27.0f * spread;
        float height     = 65.0f + 67.0f * spread;
        float shear      = 32.0f * spread;
        u64   color      = GS_SETREG_RGBAQ(colors[loop][0], colors[loop][1], colors[loop][2], UI_OVERLAY_ALPHA, 0);
        float previous_x = 0, previous_y = 0;

        for (unsigned point = 0; point < sizeof(wave_points) / sizeof(wave_points[0]); ++point)
        {
            float x_wave = wave_points[point].x / LOGO_WAVE_SCALE;
            float y_wave = wave_points[point].y / LOGO_WAVE_SCALE;
            float x      = left + (582.0f + x_wave * (radius + shear * y_wave)) * scale;
            float y      = top + (132.0f + y_wave * height) * scale;

            if (point)
            {
                gsKit_prim_line(gs, previous_x, previous_y, x, y, 1, color);
            }

            previous_x = x;
            previous_y = y;
        }
    }

    float point_scale = scale / LOGO_POINT_SCALE;
    u64   white       = ui_color(UI_COLOR_TEXT);

    for (unsigned triangle = 0; triangle < sizeof(letter_triangles) / sizeof(letter_triangles[0]); ++triangle)
    {
        const LogoTriangle* indices = &letter_triangles[triangle];
        const LogoPoint*    a       = &letter_vertices[indices->a];
        const LogoPoint*    b       = &letter_vertices[indices->b];
        const LogoPoint*    c       = &letter_vertices[indices->c];

        gsKit_prim_triangle_gouraud(gs, left + a->x * point_scale, top + a->y * point_scale, left + b->x * point_scale, top + b->y * point_scale, left + c->x * point_scale, top + c->y * point_scale, 1, white, white, white);
    }
}
