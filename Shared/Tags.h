// Copyright (C) 2024 Paul Johnson

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

// The tag of a player: a short name an account carries (the `tag` of accounts.accounts on the
// master, MasterServer/c/db.h). The game servers call it the role of the squad member
// (Shared/Squad.h): it is what the squad tooltip shows under the name. A tag that is
// "" means the account has none, which is what every account from before the tags reads as.
//
// Some tags also change the look of the flower: that is its skin. Which skin a tag gives is
// decided here, once, for the server (which sends the skin with the flower) and the client
// (which draws the squad panel from the role alone). The tags are not case sensitive, so one
// typed in the database as "spider" works.

enum rr_flower_skin
{
    rr_flower_skin_normal, // 0: the flower of everybody; also what old data means
    rr_flower_skin_spider, // 1: the flower with the legs of a spider (Client/Renderer/RenderFlower.c)

    rr_flower_skin_max
};

#define RR_TAG_SIZE 16 // with the 0 at the end, like the role of a squad member
#define RR_TAG_SPIDER "Spider"
#define RR_TAG_SUPPORTER "Supporter"

static inline int rr_tag_equals(char const *a, char const *b)
{
    for (;; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return 0;
        if (x == 0)
            return 1;
    }
}

// the skin of a flower whose player has this tag (an unknown tag is a normal flower)
static inline uint8_t rr_flower_skin_from_tag(char const *tag)
{
    if (tag != 0 && rr_tag_equals(tag, RR_TAG_SPIDER))
        return rr_flower_skin_spider;
    return rr_flower_skin_normal;
}

// A tag that is only a name (Supporter, ...) turns the flower of its player upside down, like
// a developer's. A tag that is a skin does not: the flower is as everybody's, in its skin.
static inline int rr_tag_flips_flower(char const *tag)
{
    return tag != 0 && tag[0] != 0 &&
           rr_flower_skin_from_tag(tag) == rr_flower_skin_normal;
}
