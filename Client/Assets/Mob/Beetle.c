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

// The beetle (the dark purple one of the picture of the bugs), drawn by hand in parts
// (Mob/HandDrawn.h says how). Its outlines are traced from the picture (BeetleShapes.h, made like
// the ones of Mob/Mantis.c); the colours are the ones of the picture.
// The picture of it is 130 x 184 pixels. The middle line of the bug is u 64.5, and v 105 is the
// middle of the mob: the big oval of the back is around v 111, and the two horns reach out in
// front of it, so the circle that is hit is the back with the horns a little out of it.
// 2.55 units a pixel.
#include <Client/Assets/Mob/BeetleShapes.h>

#define BT_F 2.55f
#define BT_U BEETLE_AXIS
#define BT_V 105.0f
// a horn opens and shuts around its foot, which is on the back, so that is the middle of its sprite
#define BT_HORN_U 38.0f
#define BT_HORN_V 56.0f
#define BT_DARK 0xff1b182b
#define BT_BODY 0xff292441
#define BT_LIGHT 0xff322e54

// the three legs on the right, from the front; the left ones are these turned over. The middle of a
// leg's sprite is where it is joined to the body, which is what it turns around. The legs are short,
// so they step wide (RR_HAND_PART_WIDE) to be seen going
static void beetle_leg0(struct rr_renderer *r)
{
    frame(r, BT_F, BEETLE_LEG0_ROOT_U, BEETLE_LEG0_ROOT_V);
    smooth_closed(r, BT_DARK, beetle_leg0_pts, BEETLE_LEG0_COUNT);
}

static void beetle_leg1(struct rr_renderer *r)
{
    frame(r, BT_F, BEETLE_LEG1_ROOT_U, BEETLE_LEG1_ROOT_V);
    smooth_closed(r, BT_DARK, beetle_leg1_pts, BEETLE_LEG1_COUNT);
}

static void beetle_leg2(struct rr_renderer *r)
{
    frame(r, BT_F, BEETLE_LEG2_ROOT_U, BEETLE_LEG2_ROOT_V);
    smooth_closed(r, BT_DARK, beetle_leg2_pts, BEETLE_LEG2_COUNT);
}

// the horn on the left, a dark blade that is wide at its foot and ends in a point. The body is
// drawn first and the horn over it, as in the picture. The right one is this one turned over
static void beetle_horn(struct rr_renderer *r)
{
    frame(r, BT_F, BT_HORN_U, BT_HORN_V);
    smooth_closed(r, BT_DARK, beetle_horn_pts, BEETLE_HORN_COUNT);
}

// the band that goes along the left wing cover and the line beside it; the right ones are these
// turned over
static void beetle_band(struct rr_renderer *r)
{
    frame(r, BT_F, BEETLE_BAND_U, BEETLE_BAND_V);
    smooth_closed(r, BT_LIGHT, beetle_band0_pts, BEETLE_BAND0_COUNT);
}

// the big oval of the back and the lighter shield on it, pointed at the top
static void beetle_back(struct rr_renderer *r)
{
    frame(r, BT_F, BT_U, 111.5);
    smooth_closed(r, BT_BODY, beetle_body_pts, BEETLE_BODY_COUNT);
    smooth_closed(r, BT_LIGHT, beetle_disc_pts, BEETLE_DISC_COUNT);
}

// the order of the sprites of the beetle's sheet
enum
{
    BT_LEG0,
    BT_LEG1,
    BT_LEG2,
    BT_HORN,
    BT_BAND,
    BT_BACK
};

#define BT_LEG_AT(flags, slot, n) \
    AT_WITH(flags, slot, BT_F, BT_U, BT_V, BEETLE_LEG##n##_ROOT_U, BEETLE_LEG##n##_ROOT_V)
#define BT_LEG_AT_M(flags, slot, n) \
    AT_M_WITH(flags, slot, BT_F, BT_U, BT_V, BEETLE_LEG##n##_ROOT_U, BEETLE_LEG##n##_ROOT_V)

struct rr_hand_part const RR_BEETLE_PARTS[] = {
    BT_LEG_AT(RR_HAND_PART_STEP | RR_HAND_PART_WIDE, BT_LEG0, 0),
    BT_LEG_AT_M(RR_HAND_PART_STEP | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, BT_LEG0, 0),
    BT_LEG_AT(RR_HAND_PART_STEP | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, BT_LEG1, 1),
    BT_LEG_AT_M(RR_HAND_PART_STEP | RR_HAND_PART_WIDE, BT_LEG1, 1),
    BT_LEG_AT(RR_HAND_PART_STEP | RR_HAND_PART_WIDE, BT_LEG2, 2),
    BT_LEG_AT_M(RR_HAND_PART_STEP | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, BT_LEG2, 2),
    AT(BT_BACK, BT_F, BT_U, BT_V, BT_U, 111.5),
    // the bands: the one of the picture is on its left, so this is the other side
    AT(BT_BAND, BT_F, BT_U, BT_V, BEETLE_BAND_U, BEETLE_BAND_V),
    AT_M(BT_BAND, BT_F, BT_U, BT_V, BEETLE_BAND_U, BEETLE_BAND_V),
    // the horns open and shut a little
    AT_WITH(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE, BT_HORN, BT_F, BT_U, BT_V, BT_HORN_U, BT_HORN_V),
    AT_M_WITH(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE, BT_HORN, BT_F, BT_U, BT_V, BT_HORN_U, BT_HORN_V)};

uint8_t const RR_BEETLE_PART_COUNT =
    sizeof RR_BEETLE_PARTS / sizeof *RR_BEETLE_PARTS;

// the sprites of the sheet, in the order of the enum, with the cell of each (its widest reach
// from its middle, in pixels)
void rr_beetle_sheet_init(struct rr_renderer_spritesheet *sheet)
{
#define SPRITE(reach, draw) CELL(BT_F, reach), CELL(BT_F, reach), draw
    rr_renderer_spritesheet_init(
        sheet, NULL, SPRITE(30, beetle_leg0), SPRITE(30, beetle_leg1),
        SPRITE(30, beetle_leg2), SPRITE(32, beetle_horn),
        SPRITE(60, beetle_band), SPRITE(72, beetle_back), 0);
#undef SPRITE
}
