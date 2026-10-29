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

#include <Shared/SimulationCommon.h>
#include <Shared/StaticData.h>

struct rr_component_arena;

// Picks one of the pregenerated RR_BEEHIVE_MAZE_TIERS by rarity and fills its
// open cells with a queen ant (farthest from the entrance), a ring of eggs
// around her, worker ants, baby ants and, by chance, one or two diggers. Every
// mob is fixed at exactly `rarity_id`. Called once, at container spawn time.
void rr_ant_hole_generate_layout(struct rr_simulation *simulation,
                                 EntityIdx container_id,
                                 struct rr_component_arena *arena,
                                 enum rr_rarity_id rarity_id,
                                 enum rr_simulation_team_id team_id);

// Runs when a queen ant dies inside a hole: every surviving worker, baby ant
// and digger goes out into the main world, the eggs are removed, and the hole
// itself is torn down, which ejects any players still inside.
void rr_ant_hole_queen_teardown(struct rr_simulation *simulation,
                                EntityIdx queen_id, EntityIdx container_id);

// Lays one egg at (x, y) inside a hole and keeps the hole's egg count right.
EntityIdx rr_ant_hole_alloc_egg(struct rr_simulation *simulation,
                                EntityIdx container_id, float x, float y,
                                enum rr_rarity_id rarity_id,
                                enum rr_simulation_team_id team_id);
