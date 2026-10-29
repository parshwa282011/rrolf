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

#include <Shared/Component/Common.h>
#include <Shared/Entity.h>
#include <Shared/Utilities.h>

struct rr_simulation;
struct proto_bug;
RR_CLIENT_ONLY(struct rr_renderer;)
RR_SERVER_ONLY(struct rr_component_player_info;)
RR_SERVER_ONLY(struct rr_maze_grid;)

struct rr_component_mob
{
    RR_SERVER_ONLY(struct rr_maze_grid *zone;)
    RR_SERVER_ONLY(uint16_t ticks_to_despawn;)
    // a hive or hole nobody has gone into yet unloads like any wild mob when no
    // one is near. ticks_to_despawn can't be used for that: once somebody is
    // inside it is the collapse timer
    RR_SERVER_ONLY(uint16_t unvisited_ticks;)
    EntityIdx parent_id;
    RR_SERVER_ONLY(uint8_t protocol_state;)
    uint8_t id;
    uint8_t rarity;
    RR_CLIENT_ONLY(uint8_t counted_as_killed;)
    uint8_t player_spawned : 1;
    RR_SERVER_ONLY(uint8_t no_drop : 1;)
};

void rr_component_mob_init(struct rr_component_mob *, struct rr_simulation *);
void rr_component_mob_free(struct rr_component_mob *, struct rr_simulation *);

RR_SERVER_ONLY(void rr_component_mob_write(struct rr_component_mob *,
                                           struct proto_bug *, int,
                                           struct rr_component_player_info *);)
RR_CLIENT_ONLY(void rr_component_mob_read(struct rr_component_mob *,
                                          struct proto_bug *);)

RR_DECLARE_PUBLIC_FIELD(mob, uint8_t, id)
RR_DECLARE_PUBLIC_FIELD(mob, uint8_t, rarity)
RR_DECLARE_PUBLIC_FIELD(mob, uint8_t, player_spawned)
