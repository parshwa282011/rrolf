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

// Picks one of the pregenerated RR_BEEHIVE_MAZE_TIERS by rarity, and
// populates its open cells with a core, a queen and fighter bees/honeybees
// (all fixed at exactly `rarity_id`, never scaled up or down). Called once,
// at container spawn time - beehives never repopulate on a tick.
void rr_beehive_generate_layout(struct rr_simulation *simulation,
                                EntityIdx container_id,
                                struct rr_component_arena *arena,
                                enum rr_rarity_id rarity_id,
                                enum rr_simulation_team_id team_id);

// Runs when a beehive's core dies: releases every surviving fighter
// bee/honeybee/queen into the main world outside the hive (they stay there
// until killed or unloaded, like any other wild mob) and tears down the
// container itself, which ejects any players still inside via the arena
// component's normal free callback.
void rr_beehive_core_teardown(struct rr_simulation *simulation,
                              EntityIdx core_id, EntityIdx container_id);
