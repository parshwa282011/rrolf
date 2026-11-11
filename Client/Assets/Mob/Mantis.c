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

// The mantis, drawn by hand in parts after the picture of the bugs (Mob/HandDrawn.h says how). The
// picture is 98 x 203 pixels, the numbers are places in it, and the colours are the ones of the picture
// (there is no white in it: the gaps between the arms and the neck are the page showing through).
// The head, the neck, the body, the antennae and the legs are drawn from numbers measured on the
// picture; the arm and the stripes of the abdomen are traced from it by tools/sprite-trace/trace_mantis.py
// (MantisShapes.h). The middle line of the bug is u 48.5, and v 100 is the middle of the mob. 2.55 units a pixel, as the beetle has.
#include <Client/Assets/Mob/MantisShapes.h>

#define MA_F 2.55f
#define MA_U MANTIS_AXIS
#define MA_V 100.0f
#define MA_DARK 0xff0f5118
#define MA_FORE 0xff027c1b
#define MA_BODY 0xff009922
#define MA_BRIGHT 0xff00ae22

// The antenna on the right, a long blade that is wide at its foot and thin at its tip; its sprite is
// turned around its foot. The left one is this one turned over
static float const mantis_antenna_pts[][2] = {
    {76.5, 3},  {73, 5.5},  {66, 13},   {60, 24},   {55, 36},  {51.5, 47},
    {53.5, 49}, {57.5, 41}, {63, 31},   {69, 20},   {74.5, 10}};
#define MANTIS_ANTENNA_ROOT_U 53.0f
#define MANTIS_ANTENNA_ROOT_V 46.0f

static void mantis_antenna(struct rr_renderer *r)
{
    frame(r, MA_F, MANTIS_ANTENNA_ROOT_U, MANTIS_ANTENNA_ROOT_V);
    smooth_closed(r, MA_DARK, mantis_antenna_pts, 11);
}

// the arm with its claw, traced: it turns around the shoulder
static void mantis_arm(struct rr_renderer *r)
{
    frame(r, MA_F, MANTIS_ARM_ROOT_U, MANTIS_ARM_ROOT_V);
    smooth_closed(r, MA_FORE, mantis_arm_pts, MANTIS_ARM_COUNT);
}

// the short leg behind the arm, a bar with round ends that goes out and a little down
#define MANTIS_MID_ROOT_U 60.0f
#define MANTIS_MID_ROOT_V 112.0f

static void mantis_mid(struct rr_renderer *r)
{
    frame(r, MA_F, MANTIS_MID_ROOT_U, MANTIS_MID_ROOT_V);
    limb(r, MA_DARK, 8.6, MANTIS_MID_ROOT_U, MANTIS_MID_ROOT_V, 76.5, 114.5);
}

// the leg at the back, a wedge that is wide where it leaves the abdomen
static float const mantis_rear_pts[][2] = {
    {63, 125}, {68, 128}, {75, 150}, {76, 157}, {72, 158}, {66, 150}, {61, 136}};
#define MANTIS_REAR_ROOT_U 63.0f
#define MANTIS_REAR_ROOT_V 127.0f

static void mantis_rear(struct rr_renderer *r)
{
    frame(r, MA_F, MANTIS_REAR_ROOT_U, MANTIS_REAR_ROOT_V);
    smooth_closed(r, MA_DARK, mantis_rear_pts, 7);
}

// the head, the neck and the long abdomen: the left half of the outline from the top of the head to
// the end of the abdomen (the first and the last point are on the middle line)
static float const mantis_outline[][2] = {
    {48.5, 30}, {43, 31.5}, {38, 36},    {34.5, 43},  {32.5, 50},  {35, 55.5},  {40, 58.5},
    {39.5, 66}, {39, 80},   {38.3, 96},  {38.5, 108}, {36, 118},   {32.5, 132}, {30, 148},
    {29.5, 163}, {32, 178}, {38, 190},   {45, 197},   {48.5, 198.5}};
// the lighter face, and the lighter strip down the neck
static float const mantis_face[][2] = {
    {48.5, 33}, {42, 37}, {38, 43}, {36.5, 50}, {40, 53.5}, {48.5, 55}};
static float const mantis_neck[][2] = {
    {48.5, 58}, {44, 60}, {43, 68}, {43, 100}, {45, 109.5}, {48.5, 112}};

static void mantis_body(struct rr_renderer *r)
{
    frame(r, MA_F, MA_U, 101.5);
    smooth_symmetric(r, MA_BODY, MA_U, mantis_outline, 19);
    smooth_symmetric(r, MA_BRIGHT, MA_U, mantis_face, 6);
    smooth_symmetric(r, MA_BRIGHT, MA_U, mantis_neck, 6);
    smooth_closed(r, MA_BRIGHT, mantis_bright0_pts, MANTIS_BRIGHT0_COUNT);
    smooth_closed(r, MA_BRIGHT, mantis_bright1_pts, MANTIS_BRIGHT1_COUNT);
}

// the order of the sprites of the mantis' sheet
enum
{
    MA_ANTENNA,
    MA_ARM,
    MA_MID,
    MA_REAR,
    MA_BODY_SPRITE
};

#define MA_AT(flags, slot, k) \
    AT_WITH(flags, slot, MA_F, MA_U, MA_V, MANTIS_##k##_ROOT_U, MANTIS_##k##_ROOT_V)
#define MA_AT_M(flags, slot, k) \
    AT_M_WITH(flags, slot, MA_F, MA_U, MA_V, MANTIS_##k##_ROOT_U, MANTIS_##k##_ROOT_V)

// the legs step (a wide step: they are short), the arms rock, the antennae feel about
struct rr_hand_part const RR_MANTIS_PARTS[] = {
    MA_AT(RR_HAND_PART_STEP | RR_HAND_PART_WIDE, MA_REAR, REAR),
    MA_AT_M(RR_HAND_PART_STEP | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, MA_REAR, REAR),
    MA_AT(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE, MA_ARM, ARM),
    MA_AT_M(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, MA_ARM, ARM),
    MA_AT(RR_HAND_PART_STEP | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, MA_MID, MID),
    MA_AT_M(RR_HAND_PART_STEP | RR_HAND_PART_WIDE, MA_MID, MID),
    AT(MA_BODY_SPRITE, MA_F, MA_U, MA_V, MA_U, 101.5),
    MA_AT(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE | RR_HAND_PART_ODD, MA_ANTENNA, ANTENNA),
    MA_AT_M(RR_HAND_PART_WIGGLE | RR_HAND_PART_WIDE, MA_ANTENNA, ANTENNA)};

uint8_t const RR_MANTIS_PART_COUNT =
    sizeof RR_MANTIS_PARTS / sizeof *RR_MANTIS_PARTS;

// the sprites of the sheet, in the order of the enum, with the cell of each (its widest reach
// from its middle, in pixels)
void rr_mantis_sheet_init(struct rr_renderer_spritesheet *sheet)
{
#define SPRITE(reach, draw) CELL(MA_F, reach), CELL(MA_F, reach), draw
    rr_renderer_spritesheet_init(
        sheet, NULL, SPRITE(48, mantis_antenna), SPRITE(54, mantis_arm),
        SPRITE(28, mantis_mid), SPRITE(24, mantis_rear),
        SPRITE(106, mantis_body), 0);
#undef SPRITE
}
