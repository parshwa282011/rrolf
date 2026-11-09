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

#include <stddef.h>

#include <Client/Assets/Render.h>

#include <Client/Assets/Mob/HandDrawn.h>
#include <Client/Renderer/Renderer.h>

// The lanternfly, drawn by hand after the picture of the bugs, in parts (Mob/HandDrawn.h says
// how), put together like the one it replaces: an abdomen, two wings that swing, the legs, and
// now the head. 105 x 120 pixels. The middle line of the bug is u 52.3, the middle of its height v 60.
// 2.42 units a pixel. Everything below was measured on the picture: the edges of the shell,
// the head and its diamond, the lobes of the back, the spots, the width and the ends of
// every leg.
#define LF_F 2.42f
#define LF_U 52.3f
#define LF_V 60.0f
#define LF_LEG 0xff0f1314

// the three legs on the right; the left ones are these turned over. Each is a limb of one
// width with round ends, one end under the body
static void lanternfly_leg_top(struct rr_renderer *r)
{
    frame(r, LF_F, 80, 11);
    limb(r, LF_LEG, 9.6, 89.3, 6.4, 70.8, 15.4);
}

static void lanternfly_leg_mid(struct rr_renderer *r)
{
    frame(r, LF_F, 89, 36.4);
    limb(r, LF_LEG, 9.6, 99.5, 39.2, 78.8, 33.5);
}

static void lanternfly_leg_low(struct rr_renderer *r)
{
    frame(r, LF_F, 92.5, 64);
    limb(r, LF_LEG, 9.6, 87.6, 60.8, 95.1, 70.3);
}

// the abdomen: the red one the lanternfly always had, kept as it was. It is under the wings
// and only shows when they open. It is drawn in the units of its own sprite, not the picture's
static void lanternfly_abdomen(struct rr_renderer *renderer)
{
    rr_renderer_set_fill(renderer, 0xffa42312);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -0.00, 68.72);
    rr_renderer_bezier_curve_to(renderer, -50.94, 68.72, -92.23, 37.95, -92.23,
                                -0.00);
    rr_renderer_bezier_curve_to(renderer, -92.23, -18.23, -82.52, -35.70,
                                -65.22, -48.59);
    rr_renderer_bezier_curve_to(renderer, -47.92, -61.48, -24.46, -68.72, -0.00,
                                -68.72);
    rr_renderer_bezier_curve_to(renderer, 50.94, -68.72, 92.23, -37.95, 92.23,
                                -0.00);
    rr_renderer_bezier_curve_to(renderer, 92.23, 37.95, 50.94, 68.72, -0.00,
                                68.72);
    rr_renderer_fill(renderer);
    rr_renderer_set_fill(renderer, 0xffc23523);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -0.00, 50.06);
    rr_renderer_bezier_curve_to(renderer, -39.38, 50.06, -71.31, 27.64, -71.31,
                                -0.00);
    rr_renderer_bezier_curve_to(renderer, -71.31, -13.28, -63.80, -26.01,
                                -50.42, -35.40);
    rr_renderer_bezier_curve_to(renderer, -37.05, -44.78, -18.91, -50.06, -0.00,
                                -50.06);
    rr_renderer_bezier_curve_to(renderer, 39.38, -50.06, 71.31, -27.65, 71.31,
                                -0.00);
    rr_renderer_bezier_curve_to(renderer, 71.31, 27.64, 39.38, 50.06, -0.00,
                                50.06);
    rr_renderer_fill(renderer);
}

// the edge of the back, from the top of the middle line round the left side to the bottom of it
// (the wings are cut along that line: each is one half of the back)
static float const lanternfly_edge[][2] = {
    {52.3, 31},   {35, 33},     {21, 36},     {16.4, 44},   {14.2, 52},
    {13.3, 60},   {13.2, 68},   {13.7, 76},   {15.2, 84},   {17.7, 92},
    {21.4, 100},  {27.1, 108},  {38.0, 116},  {52.3, 117.7}};

// the lighter part of a wing: the left one, from the middle line at the top, round it and back
static float const lanternfly_lobe[][2] = {
    {47.7, 38},  {30, 38},    {24.9, 41},  {23.5, 48},  {21.5, 57},
    {21.1, 65},  {21.1, 72},  {22.7, 81},  {25.3, 90},  {30.1, 99},
    {36.2, 105}, {41.3, 108}, {45.8, 109.2}, {47.7, 107.6}, {47.7, 100},
    {47.7, 80},  {47.7, 60}};

// the left wing: one half of the back, the lighter part and its three spots. It swings round
// its root, the corner next to the head where it joins the body, so that is the middle of the
// sprite
static void lanternfly_wing_left(struct rr_renderer *r)
{
    frame(r, LF_F, 47.7, 40);
    smooth_open_fill(r, 0xff917966, lanternfly_edge, 14);
    smooth_closed(r, 0xffa88c75, lanternfly_lobe, 17);
    dot(r, 0xff323a38, 33.7, 60.2, 6);
    dot(r, 0xff323a38, 33.7, 81.5, 6);
    dot(r, 0xff323a38, 42.2, 95.6, 6);
}

// the right wing: the shape of the left one turned over, and its own three spots
static void lanternfly_wing_right(struct rr_renderer *r)
{
    frame(r, LF_F, 56.9, 40);
    float edge[14][2], lobe[17][2];
    for (int i = 0; i < 14; ++i)
    {
        edge[i][0] = 104.6f - lanternfly_edge[i][0];
        edge[i][1] = lanternfly_edge[i][1];
    }
    for (int i = 0; i < 17; ++i)
    {
        lobe[i][0] = 104.6f - lanternfly_lobe[i][0];
        lobe[i][1] = lanternfly_lobe[i][1];
    }
    smooth_open_fill(r, 0xff917966, edge, 14);
    smooth_closed(r, 0xffa88c75, lobe, 17);
    dot(r, 0xff323a38, 64.3, 60.8, 6);
    dot(r, 0xff323a38, 76.3, 71.8, 6);
    dot(r, 0xff323a38, 69.7, 90, 6);
}

// the dark shield and the lighter diamond in it
static void lanternfly_head(struct rr_renderer *r)
{
    frame(r, LF_F, 52.3, 30);
    static float const shield[][2] = {
        {52.3, 3.3}, {47.4, 4},    {41.5, 7},    {37.6, 10},   {32, 14},
        {28.1, 19},  {25.6, 24},   {24.3, 29},   {24, 34},     {25.6, 37},
        {28.3, 40},  {30.4, 43},   {33, 46},     {36.5, 49},   {41, 52},
        {46, 54.3},  {52.3, 55.2}};
    smooth_symmetric(r, 0xff212526, 52.3, shield, 17);
    static float const diamond[][2] = {
        {52.3, 11.5}, {47.2, 14},   {43.3, 17},  {40, 20},   {37.4, 23},
        {34.9, 26},   {33.3, 29.5}, {33.2, 31},  {34.6, 34}, {37.3, 38},
        {39.7, 41},   {43.2, 44},   {46, 45.6},  {52.3, 46.7}};
    smooth_symmetric(r, 0xff333a3a, 52.3, diamond, 14);
}

// the order of the sprites of the lanternfly's sheet
enum
{
    LF_LEG_TOP,
    LF_LEG_MID,
    LF_LEG_LOW,
    LF_ABDOMEN,
    LF_WING_LEFT,
    LF_WING_RIGHT,
    LF_HEAD
};

struct rr_hand_part const RR_LANTERNFLY_PARTS[] = {
    AT_WIGGLE(LF_LEG_TOP, LF_F, LF_U, LF_V, 80, 11),
    AT_M_WIGGLE(LF_LEG_TOP, LF_F, LF_U, LF_V, 80, 11),
    AT_WIGGLE(LF_LEG_MID, LF_F, LF_U, LF_V, 89, 36.4),
    AT_M_WIGGLE(LF_LEG_MID, LF_F, LF_U, LF_V, 89, 36.4),
    AT_WIGGLE(LF_LEG_LOW, LF_F, LF_U, LF_V, 92.5, 64),
    AT_M_WIGGLE(LF_LEG_LOW, LF_F, LF_U, LF_V, 92.5, 64),
    AT(LF_ABDOMEN, LF_F, LF_U, LF_V, 52.3, 78),
    AT_SWING(LF_WING_LEFT, LF_F, LF_U, LF_V, 47.7, 40),
    AT_SWING(LF_WING_RIGHT, LF_F, LF_U, LF_V, 56.9, 40),
    AT(LF_HEAD, LF_F, LF_U, LF_V, 52.3, 30)};

uint8_t const RR_LANTERNFLY_PART_COUNT =
    sizeof RR_LANTERNFLY_PARTS / sizeof *RR_LANTERNFLY_PARTS;

// the sprites of the sheet, in the order of the enum, with the cell of each (its widest reach
// from its middle, in pixels)
void rr_lanternfly_sheet_init(struct rr_renderer_spritesheet *sheet)
{
#define SPRITE(reach, draw) CELL(LF_F, reach), CELL(LF_F, reach), draw
    rr_renderer_spritesheet_init(
        sheet, NULL, SPRITE(14, lanternfly_leg_top),
        SPRITE(14, lanternfly_leg_mid), SPRITE(14, lanternfly_leg_low),
        SPRITE(40, lanternfly_abdomen), SPRITE(80, lanternfly_wing_left),
        SPRITE(80, lanternfly_wing_right), SPRITE(30, lanternfly_head), 0);
#undef SPRITE
}
