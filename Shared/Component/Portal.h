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

#pragma once

#include <Shared/Component/Common.h>
#include <Shared/Entity.h>
#include <Shared/Utilities.h>

struct rr_simulation;
struct proto_bug;
RR_SERVER_ONLY(struct rr_component_player_info;)

// a portal lets a player walk from one server instance into another (e.g.
// hell_creek_easy -> hell_creek_med). target_dimension is only a label shown
// on the portal; target_server_url is never sent to clients in view - it's
// only handed to a player via rr_clientbound_redirect the moment they touch
// the portal (see Server/System/Portal.c), so it can't be read by scanning
// nearby entities.
struct rr_component_portal
{
    EntityIdx parent_id;
    RR_SERVER_ONLY(uint8_t protocol_state;)
    uint8_t rarity;
    char target_dimension[24];
    RR_SERVER_ONLY(char target_server_url[64];)
};

void rr_component_portal_init(struct rr_component_portal *,
                              struct rr_simulation *);
void rr_component_portal_free(struct rr_component_portal *,
                              struct rr_simulation *);

RR_SERVER_ONLY(void rr_component_portal_write(
                   struct rr_component_portal *, struct proto_bug *, int,
                   struct rr_component_player_info *);)
RR_CLIENT_ONLY(void rr_component_portal_read(struct rr_component_portal *,
                                             struct proto_bug *);)

RR_DECLARE_PUBLIC_FIELD(portal, uint8_t, rarity)
