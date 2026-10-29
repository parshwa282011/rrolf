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

#include <math.h>

#include <Client/Assets/Render.h>

#include <Client/Renderer/Renderer.h>

// The floor inside an ant hole: packed dirt with pebbles and the odd root. A
// single tile like the Garden's, everything but the base stays 24 units inside
// the edge so tiles butt together with no seam.

static void circle(struct rr_renderer *renderer, uint32_t color, float x,
                   float y, float r)
{
    rr_renderer_set_fill(renderer, color);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x, y, r);
    rr_renderer_fill(renderer);
}

void rr_ah_tile_draw(struct rr_renderer *renderer)
{
    rr_renderer_set_fill(renderer, 0xff5b3d24);
    rr_renderer_begin_path(renderer);
    rr_renderer_fill_rect(renderer, -128, -128, 256, 256);
    // lighter and darker patches of dirt
    circle(renderer, 0xff654528, -52, -46, 40);
    circle(renderer, 0xff654528, 56, 44, 36);
    circle(renderer, 0xff503420, 58, -58, 26);
    circle(renderer, 0xff503420, -62, 56, 24);
    circle(renderer, 0xff503420, -4, 6, 18);
    // pebbles
    static float const pebbles[8][3] = {{-84, -10, 7}, {-20, -86, 6},
                                        {84, -16, 8},  {30, 20, 5},
                                        {-40, 36, 6},  {12, 92, 7},
                                        {-92, 82, 5},  {96, 84, 6}};
    for (uint8_t i = 0; i < 8; ++i)
    {
        circle(renderer, 0xff7d5d3c, pebbles[i][0], pebbles[i][1],
               pebbles[i][2]);
        circle(renderer, 0xff94744f, pebbles[i][0] - 1.5f, pebbles[i][1] - 1.5f,
               pebbles[i][2] * 0.5f);
    }
    // a root crossing the corner
    rr_renderer_set_stroke(renderer, 0xff3f2a18);
    rr_renderer_set_line_width(renderer, 6);
    rr_renderer_set_line_cap(renderer, 1);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -100, -70);
    rr_renderer_quadratic_curve_to(renderer, -70, -30, -40, -40);
    rr_renderer_quadratic_curve_to(renderer, -10, -52, 10, -90);
    rr_renderer_stroke(renderer);
}
