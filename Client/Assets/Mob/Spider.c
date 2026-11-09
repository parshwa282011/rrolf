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

// The spider (the black widow of the picture of the bugs), drawn by hand in parts
// (Mob/HandDrawn.h says how), from plain shapes: ellipses, circles and limbs of one width.
// 97 x 108 pixels. The middle line of the bug is u 47.4, the middle of its height v 54.
// 2.69 units a pixel. Measured on the picture: the size and place of the head, the abdomen,
// the light shapes in them and the hourglass, the fangs, and the line a leg goes along.
#define SP_F 2.69f
#define SP_U 47.4f
#define SP_V 54.0f
#define SP_LEG 0xff1b232a
#define SP_BODY 0xff2a363f
#define SP_LIGHT 0xff34424d

// the four legs on the right, from the front; the left ones are these turned over. Every leg
// is one curve of one width with round ends, and its first end is under the body
static void spider_leg1(struct rr_renderer *r)
{
    frame(r, SP_F, 73, 17);
    bent(r, SP_LEG, 9.3, 64.5, 30, 71.2, 23, 76.8, 14.9, 80.6, 5.9);
}

static void spider_leg2(struct rr_renderer *r)
{
    frame(r, SP_F, 79, 32);
    bent(r, SP_LEG, 9.3, 66.5, 37, 74.3, 33.8, 82.8, 32.4, 89.9, 27.5);
}

static void spider_leg3(struct rr_renderer *r)
{
    frame(r, SP_F, 80, 49);
    bent(r, SP_LEG, 9.3, 68, 43.2, 76.5, 44.4, 83.5, 49.4, 90.5, 53.8);
}

static void spider_leg4(struct rr_renderer *r)
{
    frame(r, SP_F, 77, 62);
    bent(r, SP_LEG, 9.3, 67, 49.5, 74.7, 56.7, 82.5, 64.4, 86.5, 74.4);
}

// the fang on the right, a dark cone: the head covers its lower part. The gap between the two
// fangs is the picture's page showing
static void spider_fang(struct rr_renderer *r)
{
    frame(r, SP_F, 54.5, 19);
    START(r, SP_LEG);
    MOVE(r, 48.9, 12.4);
    CUBIC(r, 52, 12.6, 56.5, 16, 60.5, 25.5);
    LINE(r, 48.9, 25.5);
    END(r);
}

// the big round back and the lighter oval in it
static void spider_abdomen(struct rr_renderer *r)
{
    frame(r, SP_F, 47.5, 77);
    ell(r, SP_BODY, 47.5, 77, 30.5, 31.2);
    ell(r, SP_LIGHT, 47.5, 79.5, 20.7, 20);
}

// the head, and the lighter circle in it
static void spider_head(struct rr_renderer *r)
{
    frame(r, SP_F, 47.5, 43);
    ell(r, SP_BODY, 47.5, 44, 25, 24.2);
    dot(r, SP_LIGHT, 47.4, 42, 16);
}

// the red hourglass on the back: two round ends and the waist between them
static void spider_hourglass(struct rr_renderer *r)
{
    frame(r, SP_F, 47.1, 78);
    ell(r, 0xffcc0000, 47.1, 71.6, 12.4, 7.2);
    ell(r, 0xffcc0000, 47.1, 84.8, 12.4, 6.7);
    START(r, 0xffcc0000);
    MOVE(r, 39.1, 72);
    LINE(r, 55.1, 72);
    LINE(r, 55.1, 84.5);
    LINE(r, 39.1, 84.5);
    END(r);
}

// the order of the sprites of the spider's sheet
enum
{
    SP_LEG1,
    SP_LEG2,
    SP_LEG3,
    SP_LEG4,
    SP_FANG,
    SP_ABDOMEN,
    SP_HEAD,
    SP_HOURGLASS
};

struct rr_hand_part const RR_SPIDER_PARTS[] = {
    AT(SP_LEG1, SP_F, SP_U, SP_V, 73, 17),
    AT_M(SP_LEG1, SP_F, SP_U, SP_V, 73, 17),
    AT(SP_LEG2, SP_F, SP_U, SP_V, 79, 32),
    AT_M(SP_LEG2, SP_F, SP_U, SP_V, 79, 32),
    AT(SP_LEG3, SP_F, SP_U, SP_V, 80, 49),
    AT_M(SP_LEG3, SP_F, SP_U, SP_V, 80, 49),
    AT(SP_LEG4, SP_F, SP_U, SP_V, 77, 62),
    AT_M(SP_LEG4, SP_F, SP_U, SP_V, 77, 62),
    AT(SP_FANG, SP_F, SP_U, SP_V, 54.5, 19),
    AT_M(SP_FANG, SP_F, SP_U, SP_V, 54.5, 19),
    AT(SP_ABDOMEN, SP_F, SP_U, SP_V, 47.5, 77),
    AT(SP_HEAD, SP_F, SP_U, SP_V, 47.5, 43),
    AT(SP_HOURGLASS, SP_F, SP_U, SP_V, 47.1, 78)};

uint8_t const RR_SPIDER_PART_COUNT =
    sizeof RR_SPIDER_PARTS / sizeof *RR_SPIDER_PARTS;

// the sprites of the sheet, in the order of the enum, with the cell of each (its widest reach
// from its middle, in pixels)
void rr_spider_sheet_init(struct rr_renderer_spritesheet *sheet)
{
#define SPRITE(reach, draw) CELL(SP_F, reach), CELL(SP_F, reach), draw
    rr_renderer_spritesheet_init(
        sheet, NULL, SPRITE(18, spider_leg1), SPRITE(16, spider_leg2),
        SPRITE(16, spider_leg3), SPRITE(20, spider_leg4), SPRITE(14, spider_fang),
        SPRITE(33, spider_abdomen), SPRITE(28, spider_head),
        SPRITE(14, spider_hourglass), 0);
#undef SPRITE
}
