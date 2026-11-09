// Copyright (C) 2024 Paul Johnson
// Copyright (C) 2024-2025 Maxim Nesterov

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <stdint.h>

#include <Client/Assets/Mob/HandPart.h>
#include <Client/Renderer/Renderer.h>

// What the hand-drawn mobs (Mob/Lanternfly.c, Mob/Spider.c) are made with.
//
// They are drawn after a picture, in separate parts, and every number is a place in that
// picture, measured in pixels from the top left corner of the bug (u to the right, v down,
// the head at the top). frame() scales the picture's pixels to the game's units (F) and
// puts the middle of the part (cu, cv) on the origin, so that every part is a small sprite
// of its own. Where the part goes in the bug is the same middle seen from the middle of the
// bug (AT). The head of the game's mobs is at +x, which X() does for every point, so
// nothing is turned afterwards.
//
// A bug is a list of parts, drawn in order: wings, then legs and antennae (a left one is
// the right one turned over: AT_M), then the body from the back to the head.

// the picture's (u, v) as the game's (x, y)
#define X(u, v) (-(v)), (u)

static inline void frame(struct rr_renderer *r, float f, float cu, float cv)
{
    rr_renderer_scale(r, f);
    rr_renderer_translate(r, cv, -cu);
}

// where a part is in the bug, in eighths of a unit
#define AT(slot, f, bu, bv, cu, cv)                                            \
    {slot, 0, (int16_t)(-((cv) - (bv)) * (f) * 8),                             \
     (int16_t)(((cu) - (bu)) * (f) * 8)}
#define AT_M(slot, f, bu, bv, cu, cv)                                          \
    {slot, RR_HAND_PART_MIRROR, (int16_t)(-((cv) - (bv)) * (f) * 8),           \
     (int16_t)(-((cu) - (bu)) * (f) * 8)}
#define AT_SWING(slot, f, bu, bv, cu, cv)                                      \
    {slot, RR_HAND_PART_SWING, (int16_t)(-((cv) - (bv)) * (f) * 8),            \
     (int16_t)(((cu) - (bu)) * (f) * 8)}
#define AT_WIGGLE(slot, f, bu, bv, cu, cv)                                     \
    {slot, RR_HAND_PART_WIGGLE, (int16_t)(-((cv) - (bv)) * (f) * 8),           \
     (int16_t)(((cu) - (bu)) * (f) * 8)}
#define AT_M_WIGGLE(slot, f, bu, bv, cu, cv)                                   \
    {slot, RR_HAND_PART_WIGGLE | RR_HAND_PART_MIRROR,                           \
     (int16_t)(-((cv) - (bv)) * (f) * 8),                                      \
     (int16_t)(-((cu) - (bu)) * (f) * 8)}
// the size of a sprite's cell: its widest reach from its middle, in pixels
#define CELL(f, reach) ((int)((2 * (f) * (reach) + 12) / 16 + 1) * 16)

#define START(r, color)                                                        \
    do                                                                         \
    {                                                                          \
        rr_renderer_set_fill(r, color);                                        \
        rr_renderer_begin_path(r);                                             \
    } while (0)
#define MOVE(r, u, v) rr_renderer_move_to(r, X(u, v))
#define LINE(r, u, v) rr_renderer_line_to(r, X(u, v))
#define CUBIC(r, u1, v1, u2, v2, u, v)                                         \
    rr_renderer_bezier_curve_to(r, X(u1, v1), X(u2, v2), X(u, v))
#define END(r) rr_renderer_fill(r)

static inline void dot(struct rr_renderer *r, uint32_t color, float u, float v,
                       float radius)
{
    rr_renderer_set_fill(r, color);
    rr_renderer_begin_path(r);
    rr_renderer_arc(r, -v, u, radius);
    rr_renderer_fill(r);
}

// a limb with round ends
static inline void limb(struct rr_renderer *r, uint32_t color, float width,
                        float u0, float v0, float u1, float v1)
{
    rr_renderer_set_stroke(r, color);
    rr_renderer_set_line_width(r, width);
    rr_renderer_set_line_cap(r, 1);
    rr_renderer_begin_path(r);
    rr_renderer_move_to(r, X(u0, v0));
    rr_renderer_line_to(r, X(u1, v1));
    rr_renderer_stroke(r);
}


// a limb with round ends that bends once: one cubic curve
static inline void bent(struct rr_renderer *r, uint32_t color, float width,
                        float u0, float v0, float u1, float v1, float u2,
                        float v2, float u3, float v3)
{
    rr_renderer_set_stroke(r, color);
    rr_renderer_set_line_width(r, width);
    rr_renderer_set_line_cap(r, 1);
    rr_renderer_begin_path(r);
    MOVE(r, u0, v0);
    CUBIC(r, u1, v1, u2, v2, u3, v3);
    rr_renderer_stroke(r);
}

static inline void ell(struct rr_renderer *r, uint32_t color, float u, float v,
                       float rw, float rh)
{
    rr_renderer_set_fill(r, color);
    rr_renderer_begin_path(r);
    rr_renderer_ellipse(r, -v, u, rh, rw);
    rr_renderer_fill(r);
}

// a closed smooth line through points (a Catmull-Rom spline: every point is on the curve)
static inline void smooth_closed(struct rr_renderer *r, uint32_t color,
                                 float const (*p)[2], int n)
{
    START(r, color);
    MOVE(r, p[0][0], p[0][1]);
    for (int i = 0; i < n; ++i)
    {
        float const *a = p[(i + n - 1) % n], *b = p[i], *c = p[(i + 1) % n],
                    *d = p[(i + 2) % n];
        CUBIC(r, b[0] + (c[0] - a[0]) / 6, b[1] + (c[1] - a[1]) / 6,
              c[0] - (d[0] - b[0]) / 6, c[1] - (d[1] - b[1]) / 6, c[0], c[1]);
    }
    END(r);
}

// a shape that is the same on both sides of the middle line u = axis: the points of the left
// half from the top to the bottom (the first and the last are on the line), the right half
// is these turned over
static inline void smooth_symmetric(struct rr_renderer *r, uint32_t color,
                                    float axis, float const (*left)[2], int n)
{
    float p[64][2];
    int m = 0;
    for (int i = 0; i < n; ++i, ++m)
    {
        p[m][0] = left[i][0];
        p[m][1] = left[i][1];
    }
    for (int i = n - 2; i >= 1; --i, ++m)
    {
        p[m][0] = 2 * axis - left[i][0];
        p[m][1] = left[i][1];
    }
    smooth_closed(r, color, p, m);
}

// an open smooth line through points, filled: it is shut with a straight line from the last
// point to the first
static inline void smooth_open_fill(struct rr_renderer *r, uint32_t color,
                                    float const (*p)[2], int n)
{
    START(r, color);
    MOVE(r, p[0][0], p[0][1]);
    for (int i = 0; i + 1 < n; ++i)
    {
        float const *a = p[i > 0 ? i - 1 : 0], *b = p[i], *c = p[i + 1],
                    *d = p[i + 2 < n ? i + 2 : n - 1];
        CUBIC(r, b[0] + (c[0] - a[0]) / 6, b[1] + (c[1] - a[1]) / 6,
              c[0] - (d[0] - b[0]) / 6, c[1] - (d[1] - b[1]) / 6, c[0], c[1]);
    }
    END(r);
}
