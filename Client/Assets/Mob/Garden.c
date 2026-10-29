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

// Art for the garden update: the bush, ant holes, eggs, the queen's abdomen, a
// digger's claw, the snail and its hole. Everything is drawn around the origin
// and faces +x, like the other mobs.

static void circle(struct rr_renderer *renderer, uint32_t color, float x,
                   float y, float r)
{
    rr_renderer_set_fill(renderer, color);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, x, y, r);
    rr_renderer_fill(renderer);
}

static void ellipse_path(struct rr_renderer *renderer, float x, float y,
                         float rx, float ry)
{
    float kx = 0.5522847f * rx, ky = 0.5522847f * ry;
    rr_renderer_move_to(renderer, x + rx, y);
    rr_renderer_bezier_curve_to(renderer, x + rx, y + ky, x + kx, y + ry, x,
                                y + ry);
    rr_renderer_bezier_curve_to(renderer, x - kx, y + ry, x - rx, y + ky,
                                x - rx, y);
    rr_renderer_bezier_curve_to(renderer, x - rx, y - ky, x - kx, y - ry, x,
                                y - ry);
    rr_renderer_bezier_curve_to(renderer, x + kx, y - ry, x + rx, y - ky,
                                x + rx, y);
}

static void ellipse(struct rr_renderer *renderer, uint32_t color, float x,
                    float y, float rx, float ry)
{
    rr_renderer_set_fill(renderer, color);
    rr_renderer_begin_path(renderer);
    ellipse_path(renderer, x, y, rx, ry);
    rr_renderer_fill(renderer);
}

void rr_bush_draw(struct rr_renderer *renderer)
{
    circle(renderer, 0xff1b5228, 0, 0, 140);
    for (uint8_t i = 0; i < 7; ++i)
    {
        float a = i * 2 * M_PI / 7;
        circle(renderer, 0xff277238, cosf(a) * 80, sinf(a) * 80, 60);
    }
    for (uint8_t i = 0; i < 6; ++i)
    {
        float a = (i + 0.5f) * 2 * M_PI / 6;
        circle(renderer, 0xff34903f, cosf(a) * 42, sinf(a) * 42, 46);
    }
    circle(renderer, 0xff44a84f, -30, -38, 34);
    circle(renderer, 0xff56bd60, -40, -48, 16);
    // berries
    static float const berries[7][2] = {{60, -50}, {-70, 30}, {20, 80},
                                        {95, 20},  {-30, -90}, {-95, -25},
                                        {40, 30}};
    for (uint8_t i = 0; i < 7; ++i)
    {
        circle(renderer, 0xffb92e35, berries[i][0], berries[i][1], 13);
        circle(renderer, 0xffe5565b, berries[i][0] - 4, berries[i][1] - 4, 4.5f);
    }
}

void rr_ant_hole_draw(struct rr_renderer *renderer)
{
    // a mound of dirt with a dark hole in the middle
    circle(renderer, 0xff6e4b2c, 0, 0, 142);
    for (uint8_t i = 0; i < 10; ++i)
    {
        float a = i * 2 * M_PI / 10 + 0.2f;
        circle(renderer, 0xff6e4b2c, cosf(a) * 118, sinf(a) * 118,
               30 + 6 * (i % 3));
    }
    circle(renderer, 0xff5a3b22, 0, 0, 108);
    circle(renderer, 0xff7d5a37, 0, -10, 98);
    circle(renderer, 0xff5a3b22, 0, 4, 90);
    circle(renderer, 0xff1c110a, 0, 6, 74);
    circle(renderer, 0xff0c0705, 4, 10, 54);
    // pebbles and grains around the rim
    static float const pebbles[9][3] = {
        {-108, -52, 11}, {-70, 100, 9},  {96, 66, 12},
        {112, -30, 8},   {20, -118, 10}, {-120, 20, 7},
        {58, 112, 7},    {-40, -112, 7}, {118, 22, 6}};
    for (uint8_t i = 0; i < 9; ++i)
        circle(renderer, 0xff9a7650, pebbles[i][0], pebbles[i][1],
               pebbles[i][2]);
}

void rr_ant_egg_draw(struct rr_renderer *renderer)
{
    ellipse(renderer, 0xffcfc3a0, 3, 4, 66, 50);
    ellipse(renderer, 0xfff1e9d0, 0, 0, 64, 48);
    ellipse(renderer, 0xfffbf8ec, -16, -16, 30, 14);
    circle(renderer, 0xffd2c7a4, 26, 10, 5);
    circle(renderer, 0xffd2c7a4, -4, 24, 4);
    circle(renderer, 0xffd2c7a4, 36, -14, 4);
}

void rr_queen_ant_abdomen_draw(struct rr_renderer *renderer)
{
    // much bigger than a worker's: it is where the eggs come from
    ellipse(renderer, 0xff4a281c, 0, 0, 110, 92);
    ellipse(renderer, 0xff5e3324, 0, 0, 100, 82);
    ellipse(renderer, 0xff3a1f15, -62, 0, 6, 76);
    ellipse(renderer, 0xff3a1f15, -22, 0, 6, 80);
    ellipse(renderer, 0xff3a1f15, 18, 0, 6, 78);
    ellipse(renderer, 0xff3a1f15, 58, 0, 6, 68);
    ellipse(renderer, 0xff8a5a46, -14, -34, 50, 18);
}

void rr_digger_ant_claw_draw(struct rr_renderer *renderer)
{
    // a flat scoop, drawn twice at the front of the head
    rr_renderer_set_fill(renderer, 0xffc9a35a);
    rr_renderer_set_stroke(renderer, 0xff8a6a30);
    rr_renderer_set_line_width(renderer, 8);
    rr_renderer_set_line_join(renderer, 1);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 0, -12);
    rr_renderer_line_to(renderer, 62, -42);
    rr_renderer_line_to(renderer, 82, 0);
    rr_renderer_line_to(renderer, 62, 42);
    rr_renderer_line_to(renderer, 0, 12);
    rr_renderer_line_to(renderer, 0, -12);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
}

void rr_snail_body_draw(struct rr_renderer *renderer)
{
    // the foot and head, with two eye stalks
    ellipse(renderer, 0xffcbb98a, -4, 0, 100, 38);
    ellipse(renderer, 0xffddcea2, 6, -6, 92, 26);
    circle(renderer, 0xffddcea2, 92, 0, 32);
    rr_renderer_set_stroke(renderer, 0xffcbb98a);
    rr_renderer_set_line_width(renderer, 9);
    rr_renderer_set_line_cap(renderer, 1);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 100, -14);
    rr_renderer_line_to(renderer, 126, -42);
    rr_renderer_move_to(renderer, 100, 14);
    rr_renderer_line_to(renderer, 126, 42);
    rr_renderer_stroke(renderer);
    circle(renderer, 0xff1b1b1b, 128, -44, 9);
    circle(renderer, 0xff1b1b1b, 128, 44, 9);
    circle(renderer, 0xffffffff, 125, -47, 3);
    circle(renderer, 0xffffffff, 125, 41, 3);
}

void rr_snail_shell_draw(struct rr_renderer *renderer)
{
    // a spiral made of circles that drift towards the centre
    circle(renderer, 0xff8c4a14, 0, 0, 74);
    circle(renderer, 0xffb9661f, -5, -3, 62);
    circle(renderer, 0xff9a5318, -9, -6, 48);
    circle(renderer, 0xffc9782c, -12, -8, 36);
    circle(renderer, 0xffa65a1c, -14, -10, 24);
    circle(renderer, 0xffd88a38, -15, -11, 12);
}

// The place a snail run ends at. It is not a mob, the client draws it from the
// snail run packet (Client/Snail.c)
void rr_snail_hole_draw(struct rr_renderer *renderer)
{
    circle(renderer, 0xff2c5a3a, 0, 0, 150);
    for (uint8_t i = 0; i < 12; ++i)
    {
        float a = i * 2 * M_PI / 12;
        circle(renderer, i % 2 ? 0xff6f726a : 0xff84877e, cosf(a) * 112,
               sinf(a) * 112, 30);
    }
    circle(renderer, 0xff3f3a30, 0, 0, 96);
    circle(renderer, 0xff10191a, 0, 4, 80);
    circle(renderer, 0xff050b0c, 3, 8, 58);
    // a mint green glow so it can be found from far away
    rr_renderer_set_stroke(renderer, 0xff7ee0b0);
    rr_renderer_set_line_width(renderer, 8);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 4, 80);
    rr_renderer_stroke(renderer);
}
