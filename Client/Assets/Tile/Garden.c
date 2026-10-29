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

// The one Garden ground tile, 256x256 around the origin. Every shape except the
// base stays at least 24 units inside the edge, so the tile can be repeated
// with no seam. It is used for the whole Garden, see RenderArena.c.

static void blob(struct rr_renderer *renderer, uint32_t color, float x,
                 float y, float r)
{
    // a lumpy circle: one big circle and three smaller ones on its rim
    rr_renderer_set_fill(renderer, color);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x, y, r);
    rr_renderer_fill(renderer);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x + r * 0.6f, y - r * 0.3f, r * 0.55f);
    rr_renderer_fill(renderer);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x - r * 0.5f, y + r * 0.4f, r * 0.6f);
    rr_renderer_fill(renderer);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x + r * 0.1f, y + r * 0.7f, r * 0.5f);
    rr_renderer_fill(renderer);
}

static void tuft(struct rr_renderer *renderer, float x, float y)
{
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, x, y);
    rr_renderer_line_to(renderer, x - 6, y - 13);
    rr_renderer_move_to(renderer, x, y);
    rr_renderer_line_to(renderer, x + 1, y - 16);
    rr_renderer_move_to(renderer, x, y);
    rr_renderer_line_to(renderer, x + 7, y - 12);
    rr_renderer_stroke(renderer);
}

static void clover(struct rr_renderer *renderer, float x, float y)
{
    rr_renderer_set_fill(renderer, 0xff3d9a55);
    for (uint8_t i = 0; i < 3; ++i)
    {
        float angle = i * 2.0943951f - 1.5707963f;
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, x + 5.5f * cosf(angle),
                        y + 5.5f * sinf(angle), 5.5f);
        rr_renderer_fill(renderer);
    }
}

static void daisy(struct rr_renderer *renderer, float x, float y)
{
    rr_renderer_set_fill(renderer, 0xfff4f1de);
    for (uint8_t i = 0; i < 5; ++i)
    {
        float angle = i * 1.2566371f;
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, x + 5.5f * cosf(angle),
                        y + 5.5f * sinf(angle), 3.6f);
        rr_renderer_fill(renderer);
    }
    rr_renderer_set_fill(renderer, 0xfff2c94c);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x, y, 2.8f);
    rr_renderer_fill(renderer);
}

void rr_ga_tile_draw(struct rr_renderer *renderer)
{
    rr_renderer_set_fill(renderer, 0xff2c773f);
    rr_renderer_begin_path(renderer);
    rr_renderer_fill_rect(renderer, -128, -128, 256, 256);

    // soft light patches
    blob(renderer, 0xff318647, -56, -50, 38);
    blob(renderer, 0xff318647, 50, 40, 42);
    blob(renderer, 0xff318647, -16, 72, 26);
    // darker patches
    blob(renderer, 0xff2f8043, 62, -62, 20);
    blob(renderer, 0xff2f8043, -72, 50, 18);

    rr_renderer_set_stroke(renderer, 0xff25663a);
    rr_renderer_set_line_width(renderer, 3);
    rr_renderer_set_line_cap(renderer, 1);
    tuft(renderer, -82, -4);
    tuft(renderer, -12, -74);
    tuft(renderer, 80, -2);
    tuft(renderer, 22, 14);
    tuft(renderer, -40, 24);
    tuft(renderer, 14, 90);

    clover(renderer, -28, -20);
    clover(renderer, 66, 70);

    daisy(renderer, -84, -64);
    daisy(renderer, 34, -26);
    daisy(renderer, -4, 50);
}
