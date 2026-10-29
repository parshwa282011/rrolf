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

#include <Server/System/AntHoleGen.h>

#include <math.h>
#include <stdlib.h>

#include <Server/EntityAllocation.h>
#include <Shared/Component/Arena.h>
#include <Shared/Component/Mob.h>
#include <Shared/Utilities.h>

struct open_cell
{
    uint32_t x;
    uint32_t y;
    float dist_sq; // to whatever the cells are currently sorted around
};

static int by_distance(void const *a, void const *b)
{
    float da = ((struct open_cell const *)a)->dist_sq;
    float db = ((struct open_cell const *)b)->dist_sq;
    return (da > db) - (da < db);
}

static void sort_around(struct open_cell *cells, uint32_t count, uint32_t x,
                        uint32_t y)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        float dx = (float)cells[i].x - x, dy = (float)cells[i].y - y;
        cells[i].dist_sq = dx * dx + dy * dy;
    }
    qsort(cells, count, sizeof *cells, by_distance);
}

EntityIdx rr_ant_hole_alloc_egg(struct rr_simulation *simulation,
                                EntityIdx container_id, float x, float y,
                                enum rr_rarity_id rarity_id,
                                enum rr_simulation_team_id team_id)
{
    struct rr_component_arena *arena =
        rr_simulation_get_arena(simulation, container_id);
    ++arena->mob_count;
    ++arena->ant_egg_count;
    return rr_simulation_alloc_mob(simulation, container_id, x, y,
                                   rr_mob_id_ant_egg, rarity_id, team_id);
}

void rr_ant_hole_generate_layout(struct rr_simulation *simulation,
                                 EntityIdx container_id,
                                 struct rr_component_arena *arena,
                                 enum rr_rarity_id rarity_id,
                                 enum rr_simulation_team_id team_id)
{
    struct rr_ant_hole_rarity_scale const *scale =
        &RR_ANT_HOLE_RARITY_SCALING[rarity_id];
    rr_component_arena_set_size_tier(arena, scale->size_tier);
    struct rr_maze_declaration *maze =
        &RR_BEEHIVE_MAZE_TIERS[scale->size_tier];
    rr_component_arena_spatial_hash_init_custom(arena, simulation, maze);

    struct open_cell *cells =
        malloc(maze->maze_dim * maze->maze_dim * sizeof *cells);
    uint32_t count = 0;
    for (uint32_t x = 0; x < maze->maze_dim; ++x)
        for (uint32_t y = 0; y < maze->maze_dim; ++y)
        {
            uint8_t v = rr_component_arena_get_grid(arena, x, y)->value;
            if (v == 0 || (v & 8))
                continue;
            cells[count].x = x;
            cells[count].y = y;
            ++count;
        }
    if (count == 0)
    {
        free(cells);
        return;
    }

    // players come in just inside a corner of the border, the queen is as far
    // from that as the layout allows
    sort_around(cells, count, 1, 1);
    struct open_cell entry = cells[0];
    struct open_cell queen = cells[count - 1];
    arena->respawn_zone.x = (entry.x + 0.5f) * maze->grid_size;
    arena->respawn_zone.y = (entry.y + 0.5f) * maze->grid_size;

    ++arena->mob_count;
    rr_simulation_alloc_mob(simulation, container_id,
                            (queen.x + 0.5f) * maze->grid_size,
                            (queen.y + 0.5f) * maze->grid_size,
                            rr_mob_id_queen_ant, rarity_id, team_id);

    // the egg chamber: the open cells closest to the queen, never her own
    sort_around(cells, count, queen.x, queen.y);
    uint32_t eggs = scale->egg_count;
    for (uint32_t i = 1; i < count && eggs > 0; ++i, --eggs)
        rr_ant_hole_alloc_egg(
            simulation, container_id,
            (cells[i].x + 0.25f + 0.5f * rr_frand()) * maze->grid_size,
            (cells[i].y + 0.25f + 0.5f * rr_frand()) * maze->grid_size,
            rarity_id, team_id);

    // everyone else is anywhere but the chamber and the entrance
    uint32_t spread = count > 12 ? count - 12 : count;
    for (uint32_t i = 0; i < scale->worker_count + scale->baby_ant_count; ++i)
    {
        // cells[] is sorted around the queen, so skip the nearest few
        struct open_cell cell = cells[count - 1 - rand() % spread];
        enum rr_mob_id id = i < scale->worker_count ? rr_mob_id_ant
                                                    : rr_mob_id_baby_ant;
        ++arena->mob_count;
        rr_simulation_alloc_mob(simulation, container_id,
                                (cell.x + rr_frand()) * maze->grid_size,
                                (cell.y + rr_frand()) * maze->grid_size, id,
                                rarity_id, team_id);
    }

    // at most two diggers: a chance for the first, and only then for the second
    uint32_t diggers = 0;
    if (rr_frand() < scale->digger_first_chance)
    {
        ++diggers;
        if (rr_frand() < scale->digger_second_chance)
            ++diggers;
    }
    for (uint32_t i = 0; i < diggers; ++i)
    {
        struct open_cell cell = cells[count - 1 - rand() % spread];
        ++arena->mob_count;
        rr_simulation_alloc_mob(simulation, container_id,
                                (cell.x + rr_frand()) * maze->grid_size,
                                (cell.y + rr_frand()) * maze->grid_size,
                                rr_mob_id_digger_ant, rarity_id, team_id);
    }

    free(cells);
}

void rr_ant_hole_queen_teardown(struct rr_simulation *simulation,
                                EntityIdx queen_id, EntityIdx container_id)
{
    struct rr_component_physical *container_physical =
        rr_simulation_get_physical(simulation, container_id);

    for (uint32_t i = 0; i < simulation->physical_count; ++i)
    {
        EntityIdx e = simulation->physical_vector[i];
        if (e == queen_id || e == container_id)
            continue;
        struct rr_component_physical *physical =
            rr_simulation_get_physical(simulation, e);
        if (physical->arena != container_id)
            continue;
        if (!rr_simulation_has_mob(simulation, e))
            continue; // players and drops are moved by the hole's own free
                      // callback, to the same spot
        struct rr_component_mob *mob = rr_simulation_get_mob(simulation, e);

        // workers, babies and diggers go outside and stay there until killed
        // or unloaded, like any wild mob. Eggs do not survive the hole
        if (mob->id == rr_mob_id_ant || mob->id == rr_mob_id_baby_ant ||
            mob->id == rr_mob_id_digger_ant)
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
