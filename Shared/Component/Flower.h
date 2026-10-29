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

#define is_dead_flower(simulation, entity)                                     \
    (rr_simulation_has_flower(simulation, entity) &&                           \
     rr_simulation_get_flower(simulation, entity)->dead)

struct rr_simulation;
struct proto_bug;
RR_CLIENT_ONLY(struct rr_renderer;)
RR_SERVER_ONLY(struct rr_component_player_info;)

struct rr_component_flower
{
    EntityIdx parent_id;
    uint8_t dead;
    uint8_t face_flags;
    uint8_t crest_count;
    uint8_t third_eye_count;
    uint8_t nazar_charges; // hits the nazar petals still block, see Health.h
    RR_SERVER_ONLY(uint8_t protocol_state;)
    RR_SERVER_ONLY(float saved_angle;)
    float eye_angle;
    uint32_t level;
    RR_CLIENT_ONLY(float eye_x;)
    RR_CLIENT_ONLY(float lerp_eye_x;)
    RR_CLIENT_ONLY(float eye_y;)
    RR_CLIENT_ONLY(float lerp_eye_y;)
    RR_CLIENT_ONLY(float lerp_mouth;)
    char nickname[16];
};

void rr_component_flower_init(struct rr_component_flower *,
                              struct rr_simulation *);
void rr_component_flower_free(struct rr_component_flower *,
                              struct rr_simulation *);

RR_SERVER_ONLY(void rr_component_flower_set_dead(struct rr_component_flower *,
                                                 struct rr_simulation *,
                                                 uint8_t);)

RR_SERVER_ONLY(void rr_component_flower_write(
                   struct rr_component_flower *, struct proto_bug *, int,
                   struct rr_component_player_info *);)
RR_CLIENT_ONLY(void rr_component_flower_read(struct rr_component_flower *,
                                             struct proto_bug *);)

RR_DECLARE_PUBLIC_FIELD(flower, uint8_t, face_flags)
RR_DECLARE_PUBLIC_FIELD(flower, uint32_t, level)
RR_DECLARE_PUBLIC_FIELD(flower, float, eye_angle)
RR_DECLARE_PUBLIC_FIELD(flower, uint8_t, crest_count)
RR_DECLARE_PUBLIC_FIELD(flower, uint8_t, third_eye_count)
RR_DECLARE_PUBLIC_FIELD(flower, uint8_t, nazar_charges)
