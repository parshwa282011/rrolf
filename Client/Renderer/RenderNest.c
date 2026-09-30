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

#include <Client/Renderer/ComponentRender.h>

#include <Client/Assets/Render.h>
#include <Client/Assets/RenderFunctions.h>
#include <Client/Game.h>

void rr_component_nest_render(EntityIdx entity, struct rr_game *game,
                              struct rr_simulation *simulation)
{
    struct rr_renderer *renderer = game->renderer;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);

    struct rr_component_nest *nest = rr_simulation_get_nest(simulation, entity);

    rr_renderer_set_global_alpha(renderer, 1 - physical->deletion_animation);
    rr_renderer_scale(renderer, 1 + physical->deletion_animation * 0.5);
    rr_renderer_rotate(renderer, physical->lerp_angle);
    rr_renderer_scale(renderer, physical->lerp_radius / 250);
    if (nest->is_beehive_post)
        rr_beehive_post_draw(renderer);
    else
        rr_renderer_draw_nest(renderer);
}
