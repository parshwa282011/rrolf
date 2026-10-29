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

#include <stdint.h>

#include <Shared/Entity.h>
#include <Shared/Snail.h>

struct rr_server;
struct rr_server_client;

// The snail run, a world event of the Garden. Every few minutes a snail turns
// up somewhere in the garden and slowly crawls to the snail hole, and the
// garden's bugs go after it. It is on the players' side, so Mint heals it. When
// it gets to the hole everyone near it is rewarded (shell, mint, nazar, see
// RR_SNAIL_NAZAR_CHANCE), and when it is eaten nobody is.
//
// The snail is a normal mob. Its way to the hole is a breadth first search
// over the map's template (SnailPath in Snail.c), the hole is not an entity,
// the client draws it from the state packet.

#define RR_SNAIL_TEMPLATE_DIM 40

struct rr_snail_event
{
    uint8_t type;
    uint8_t rarity;
    uint8_t count;
};

// one snail on its way
struct rr_snail_run
{
    // slot 0: idle (waiting for the next run), running, arrived or failed (shown
    // for a few seconds). The other slots are idle or running
    uint8_t phase;
    uint8_t rarity;
    // slot 0 only. idle: ticks until the next run. arrived and failed: ticks
    // until idle
    uint32_t phase_ticks;
    uint32_t ticks_running;
    uint32_t ticks_limit;
    EntityIdx snail_id;
    EntityHash snail;
    // asleep until a player gets near, with nothing hurting it
    uint8_t waiting;
    uint32_t waiting_ticks;
    float wait_x; // where it sleeps
    float wait_y;
    uint16_t route_cells; // from where it started
    uint16_t cells_left;
    uint8_t hp_percent;
    float snail_x;
    float snail_y;
    uint32_t quiet_ticks;
    uint32_t stuck_ticks;
    int32_t last_cell;
};

struct rr_snail
{
    struct rr_snail_run runs[RR_SNAIL_MAX_RUNS];
    float hole_x;
    float hole_y;
    uint16_t event_seq;
    struct rr_snail_event events[RR_SNAIL_EVENT_HISTORY];
    // walking distance in template cells to the hole, 255 for walls
    uint8_t field[RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM];
    uint8_t field_ready;
    uint32_t ticks; // for the packets, which go out every 5th tick
};

void rr_snail_init(struct rr_server *);
void rr_snail_tick(struct rr_server *);
void rr_snail_send_state(struct rr_server *, struct rr_server_client *);
// the dev panel: start another snail now, as long as there is a free slot (the
// timed event keeps its own). Returns 0 if there is none
uint8_t rr_snail_dev_start(struct rr_server *, uint8_t rarity);
// is this mob a snail that is asleep
uint8_t rr_snail_is_asleep(struct rr_server *, EntityIdx mob);
