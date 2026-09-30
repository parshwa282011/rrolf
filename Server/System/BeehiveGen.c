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

#include <Server/System/BeehiveGen.h>

#include <math.h>
#include <stdlib.h>

#include <Server/EntityAllocation.h>
#include <Shared/Component/Mob.h>
#include <Shared/Utilities.h>

void rr_beehive_generate_layout(struct rr_simulation *simulation,
                                EntityIdx container_id,
                                struct rr_component_arena *arena,
                                enum rr_rarity_id rarity_id,
                                enum rr_simulation_team_id team_id)
{
    struct rr_beehive_rarity_scale const *scale =
        &RR_BEEHIVE_RARITY_SCALING[rarity_id];
    rr_component_arena_set_size_tier(arena, scale->size_tier);
    struct rr_maze_declaration *maze =
        &RR_BEEHIVE_MAZE_TIERS[scale->size_tier];
    rr_component_arena_spatial_hash_init_custom(arena, simulation, maze);

    uint32_t cell_capacity = maze->maze_dim * maze->maze_dim;
    uint32_t *open_x = malloc(cell_capacity * sizeof *open_x);
    uint32_t *open_y = malloc(cell_capacity * sizeof *open_y);
    uint32_t open_count = 0;
    for (uint32_t x = 0; x < maze->maze_dim; ++x)
        for (uint32_t y = 0; y < maze->maze_dim; ++y)
        {
            uint8_t v = rr_component_arena_get_grid(arena, x, y)->value;
            if (v == 0 || (v & 8))
                continue;
            open_x[open_count] = x;
            open_y[open_count] = y;
            ++open_count;
        }
    if (open_count == 0)
    {
        free(open_x);
        free(open_y);
        return;
    }

    // players enter just inside a corner of the border, away from the boss
    uint32_t entry_idx = 0;
    float best_entry = 1e18f;
    for (uint32_t i = 0; i < open_count; ++i)
    {
        float dx = (float)open_x[i] - 1.0f, dy = (float)open_y[i] - 1.0f;
        float d = dx * dx + dy * dy;
        if (d < best_entry)
        {
            best_entry = d;
            entry_idx = i;
        }
    }
    arena->respawn_zone.x = (open_x[entry_idx] + 0.5f) * maze->grid_size;
    arena->respawn_zone.y = (open_y[entry_idx] + 0.5f) * maze->grid_size;

    // core spawns at a random open cell, as long as it's far enough from the
    // entry that players don't stumble into it the moment they walk in
    float min_core_dist = maze->maze_dim * 0.5f;
    float min_core_dist_sq = min_core_dist * min_core_dist;
    uint32_t *far_enough = malloc(open_count * sizeof *far_enough);
    uint32_t far_enough_count = 0;
    for (uint32_t i = 0; i < open_count; ++i)
    {
        float dx = (float)open_x[i] - open_x[entry_idx];
        float dy = (float)open_y[i] - open_y[entry_idx];
        if (dx * dx + dy * dy >= min_core_dist_sq)
            far_enough[far_enough_count++] = i;
    }
    uint32_t core_idx;
    if (far_enough_count > 0)
        core_idx = far_enough[rand() % far_enough_count];
    else
    {
        // burrow too small to clear min_core_dist anywhere - fall back to
        // whichever open cell is farthest from the entry
        core_idx = entry_idx;
        float best_dist = -1;
        for (uint32_t i = 0; i < open_count; ++i)
        {
            float dx = (float)open_x[i] - open_x[entry_idx];
            float dy = (float)open_y[i] - open_y[entry_idx];
            float d = dx * dx + dy * dy;
            if (d > best_dist)
            {
                best_dist = d;
                core_idx = i;
            }
        }
    }
    free(far_enough);

    // queen sits as far from the core as the layout allows
    uint32_t queen_idx = core_idx;
    if (open_count > 1)
    {
        float best_far = -1;
        for (uint32_t i = 0; i < open_count; ++i)
        {
            if (i == core_idx)
                continue;
            float dx = (float)open_x[i] - open_x[core_idx];
            float dy = (float)open_y[i] - open_y[core_idx];
            float d = dx * dx + dy * dy;
            if (d > best_far)
            {
                best_far = d;
                queen_idx = i;
            }
        }
    }

    ++arena->mob_count;
    rr_simulation_alloc_mob(
        simulation, container_id, (open_x[core_idx] + 0.5f) * maze->grid_size,
        (open_y[core_idx] + 0.5f) * maze->grid_size, rr_mob_id_beehive_core,
        rarity_id, team_id);
    if (queen_idx != core_idx)
    {
        ++arena->mob_count;
        rr_simulation_alloc_mob(simulation, container_id,
                                (open_x[queen_idx] + 0.5f) * maze->grid_size,
                                (open_y[queen_idx] + 0.5f) * maze->grid_size,
                                rr_mob_id_queen_bee, rarity_id, team_id);
    }

    uint32_t total_bees = scale->fighter_bee_count + scale->honeybee_count;
    uint32_t fighters_left = scale->fighter_bee_count;
    for (uint32_t i = 0; i < total_bees; ++i)
    {
        uint32_t idx = rand() % open_count;
        if (idx == core_idx || idx == queen_idx)
            continue;
        enum rr_mob_id id;
        if (fighters_left > 0)
        {
            id = rr_mob_id_fighter_bee;
            --fighters_left;
        }
        else
            id = rr_mob_id_honeybee;
        ++arena->mob_count;
        rr_simulation_alloc_mob(
            simulation, container_id, (open_x[idx] + rr_frand()) * maze->grid_size,
            (open_y[idx] + rr_frand()) * maze->grid_size, id, rarity_id,
            team_id);
    }

    free(open_x);
    free(open_y);
}

void rr_beehive_core_teardown(struct rr_simulation *simulation,
                              EntityIdx core_id, EntityIdx container_id)
{
    struct rr_component_physical *container_physical =
        rr_simulation_get_physical(simulation, container_id);

    for (uint32_t i = 0; i < simulation->physical_count; ++i)
    {
        EntityIdx e = simulation->physical_vector[i];
        if (e == core_id || e == container_id)
            continue;
        struct rr_component_physical *physical =
            rr_simulation_get_physical(simulation, e);
        if (physical->arena != container_id)
            continue;
        if (!rr_simulation_has_mob(simulation, e))
            continue; // players and drops get relocated by the container's
                      // own free callback below, to the exact same spot -
                      // that's what puts uncollected drops right where the
                      // ejected players land
        struct rr_component_mob *mob = rr_simulation_get_mob(simulation, e);

        // every surviving bee/queen goes outside and stays there until
        // killed by a player or naturally unloaded, like any wild mob
        if (mob->id == rr_mob_id_queen_bee || mob->id == rr_mob_id_fighter_bee ||
            mob->id == rr_mob_id_honeybee)
        {
            physical->arena = 1;
            rr_component_physical_set_x(physical, container_physical->x);
            rr_component_physical_set_y(physical, container_physical->y);
            float angle = rr_frand() * M_PI * 2;
            float v = rr_frand() * 5;
            physical->velocity.x = cosf(angle) * v;
            physical->velocity.y = sinf(angle) * v;
        }
        else
        {
            mob->no_drop = 1;
            rr_simulation_request_entity_deletion(simulation, e);
        }
    }

    rr_simulation_request_entity_deletion(simulation, container_id);
}
