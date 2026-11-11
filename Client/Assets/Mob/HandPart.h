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

#include <math.h>
#include <stdint.h>

// a part of a bug: which sprite of its sheet, where it goes (eighths of a unit, before the
// scale of the mob) and how
#define RR_HAND_PART_MIRROR 1 // the sprite turned over: the other side of the bug
#define RR_HAND_PART_SWING 2  // a wing that opens and shuts around the middle of its sprite
#define RR_HAND_PART_WIGGLE 4 // a leg that rocks a little around the middle of its sprite
#define RR_HAND_PART_ODD 8    // rocks half a round later, so that the legs next to it go the other way
#define RR_HAND_PART_STEP 16  // a leg that steps forward and back around the end of its sprite, once a round

struct rr_hand_part
{
    uint8_t slot;
    uint8_t flags;
    int16_t x;
    int16_t y;
};

// How far a part is turned (in radians) when the mob's animation timer says `timer`. At 0 every
// part is at rest, which is how the picture shows the bug, and it goes round from there. A
// wing opens as far as 0.55 and shuts again once a round; a leg rocks twice a round. The parts
// on the left of the bug turn one way, the ones on the right the other way.
static inline float rr_hand_part_angle(struct rr_hand_part const *part,
                                       float timer)
{
    float side = part->y < 0 ? 1.0f : -1.0f;
    float angle = 0;
    float phase = part->flags & RR_HAND_PART_ODD ? 3.14159265f : 0;
    if (part->flags & RR_HAND_PART_SWING)
        angle = side * 0.275f * (1 - cosf(timer));
    else if (part->flags & RR_HAND_PART_STEP)
        angle = side * 0.22f * sinf(timer + phase);
    else if (part->flags & RR_HAND_PART_WIGGLE)
        angle = side * 0.1f * sinf(2 * timer + phase);
    // a turned over sprite is turned over with its angle too
    return part->flags & RR_HAND_PART_MIRROR ? -angle : angle;
}
