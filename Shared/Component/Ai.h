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
#include <Shared/Vector.h>

struct rr_simulation;
struct proto_bug;
RR_CLIENT_ONLY(struct rr_renderer;)
RR_SERVER_ONLY(struct rr_component_player_info;)

enum rr_ai_type
{
    rr_ai_type_none,
    rr_ai_type_passive,
    rr_ai_type_neutral,
    rr_ai_type_aggro
};

enum rr_ai_state
{
    rr_ai_state_idle,
    rr_ai_state_idle_moving,
    rr_ai_state_charging,
    rr_ai_state_attacking,

    rr_ai_state_waiting_to_attack,
    rr_ai_state_exotic_special,

    rr_ai_state_returning_to_owner,
    rr_ai_state_returning_to_higher_zone
};

struct rr_component_ai
{
    RR_SERVER_ONLY(uint32_t ticks_until_next_action;)
    RR_SERVER_ONLY(EntityHash target_entity;)
    EntityIdx parent_id;
    RR_SERVER_ONLY(enum rr_ai_type ai_type;)
    RR_SERVER_ONLY(enum rr_ai_state ai_state;)
    RR_SERVER_ONLY(uint8_t protocol_state;)
    RR_SERVER_ONLY(uint8_t has_prediction;)
    RR_SERVER_ONLY(float aggro_range;)
    RR_SERVER_ONLY(struct rr_vector return_pos;)
    // for the mobs of the garden update: the queen ant's egg timer, an egg's
    // hatch timer, how long a digger ant has left to dig. special_cooldown is
    // the pause before it digs again
    RR_SERVER_ONLY(uint32_t special_ticks;)
    RR_SERVER_ONLY(uint32_t special_cooldown;)
};

void rr_component_ai_init(struct rr_component_ai *, struct rr_simulation *);
void rr_component_ai_free(struct rr_component_ai *, struct rr_simulation *);

RR_SERVER_ONLY(void rr_component_ai_write(struct rr_component_ai *,
                                          struct proto_bug *, int,
                                          struct rr_component_player_info *);)
RR_CLIENT_ONLY(void rr_component_ai_read(struct rr_component_ai *,
                                         struct proto_bug *);)
