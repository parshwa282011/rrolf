// Copyright (C) 2024  Paul Johnson

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

#include <Server/System/System.h>

#include <Server/Server.h>
#include <Server/Simulation.h>
#include <Shared/pb.h>

struct portal_touch_captures
{
    struct rr_simulation *simulation;
    EntityIdx self;
};

static void portal_touch(EntityIdx entity, void *_captures)
{
    struct portal_touch_captures *captures = _captures;
    struct rr_simulation *this = captures->simulation;
    EntityIdx portal_id = captures->self;
    struct rr_component_portal *portal =
        rr_simulation_get_portal(this, portal_id);
    struct rr_component_physical *portal_physical =
        rr_simulation_get_physical(this, portal_id);

    struct rr_component_relations *flower_relations =
        rr_simulation_get_relations(this, entity);
    if (!rr_simulation_entity_alive(this, flower_relations->owner))
        return;
    struct rr_component_player_info *player_info =
        rr_simulation_get_player_info(this, flower_relations->owner);
    if (player_info->redirecting || player_info->client == NULL)
        return;

    struct rr_component_physical *flower_physical =
        rr_simulation_get_physical(this, entity);
    struct rr_vector delta = {portal_physical->x - flower_physical->x,
                              portal_physical->y - flower_physical->y};
    if (rr_vector_magnitude_cmp(&delta, portal_physical->radius) == 1)
        return;

    player_info->redirecting = 1;
    struct proto_bug encoder;
    proto_bug_init(&encoder, outgoing_message);
    proto_bug_write_uint8(&encoder, rr_clientbound_redirect, "header");
    proto_bug_write_string(&encoder, portal->target_server_url, 64,
                           "server url");
    rr_server_client_write_message(player_info->client, encoder.start,
                                   encoder.current - encoder.start);
}

static void portal_tick(EntityIdx entity, void *_captures)
{
    struct rr_simulation *this = _captures;
    struct portal_touch_captures captures;
    captures.self = entity;
    captures.simulation = this;
    rr_simulation_for_each_flower(this, &captures, portal_touch);
}

void rr_system_portal_tick(struct rr_simulation *this)
{
    rr_simulation_for_each_portal(this, this, portal_tick);
}
