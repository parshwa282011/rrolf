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

// The ants of an ant hole: the queen who stays by her eggs and keeps laying
// them, and the eggs that hatch into baby ants.

#include <Server/MobAi/Ai.h>

#include <math.h>

#include <Server/EntityAllocation.h>
#include <Server/Simulation.h>
#include <Server/System/AntHoleGen.h>

// how far the queen strays from her chamber
#define QUEEN_LEASH 900.0f
#define QUEEN_EGG_PERIOD (8 * 25)

static void steer_towards(struct rr_component_physical *physical, float x,
                          float y, float speed)
{
    struct rr_vector delta = {x - physical->x, y - physical->y};
    rr_component_physical_set_angle(
        physical, rr_angle_lerp(physical->angle, rr_vector_theta(&delta), 0.4));
    struct rr_vector accel;
    rr_vector_from_polar(&accel, speed, physical->angle);
    rr_vector_add(&physical->acceleration, &accel);
}

void tick_ai_queen_ant(EntityIdx entity, struct rr_simulation *simulation)
{
    struct rr_component_ai *ai = rr_simulation_get_ai(simulation, entity);
    struct rr_component_mob *mob = rr_simulation_get_mob(simulation, entity);
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);
    struct rr_component_relations *relations =
        rr_simulation_get_relations(simulation, entity);

    // lay an egg whenever the hole has fewer than it should. A queen who was
    // left outside when her hole collapsed (arena 1) just fights
    if (physical->arena != 1 &&
        rr_simulation_has_arena(simulation, physical->arena))
    {
        struct rr_component_arena *arena =
            rr_simulation_get_arena(simulation, physical->arena);
        if (ai->special_ticks > 0)
            --ai->special_ticks;
        else if (arena->ant_egg_count <
                 RR_ANT_HOLE_RARITY_SCALING[mob->rarity].egg_count)
        {
            ai->special_ticks = QUEEN_EGG_PERIOD;
            float angle = rr_frand() * 2 * M_PI;
            float away = physical->radius * 1.3f;
            rr_ant_hole_alloc_egg(
                simulation, physical->arena, physical->x + cosf(angle) * away,
                physical->y + sinf(angle) * away, mob->rarity, relations->team);
        }
        else
            ai->special_ticks = 25; // all eggs are alive, look again in a second
    }

    struct rr_vector home_delta = {ai->return_pos.x - physical->x,
                                   ai->return_pos.y - physical->y};
    if (rr_vector_magnitude_cmp(&home_delta, QUEEN_LEASH) == 1)
    {
        // too far from her chamber: forget the target and walk back
        ai->target_entity = RR_NULL_ENTITY;
        ai->ai_state = rr_ai_state_idle;
        steer_towards(physical, ai->return_pos.x, ai->return_pos.y,
                      RR_PLAYER_SPEED * 0.8f);
        return;
    }
    tick_ai_default(entity, simulation, RR_PLAYER_SPEED * 0.7f);
}

void tick_ai_ant_egg(EntityIdx entity, struct rr_simulation *simulation)
{
    struct rr_component_ai *ai = rr_simulation_get_ai(simulation, entity);
    struct rr_component_mob *mob = rr_simulation_get_mob(simulation, entity);
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);
    struct rr_component_relations *relations =
        rr_simulation_get_relations(simulation, entity);
    if (ai->special_ticks > 0 && --ai->special_ticks > 0)
        return;
    // hatched. The baby counts towards its hole like every other mob in it
    struct rr_component_arena *arena =
        rr_simulation_get_arena(simulation, physical->arena);
    ++arena->mob_count;
    rr_simulation_alloc_mob(simulation, physical->arena, physical->x,
                            physical->y, rr_mob_id_baby_ant, mob->rarity,
                            relations->team);
    mob->no_drop = 1;
    rr_simulation_request_entity_deletion(simulation, entity);
}
