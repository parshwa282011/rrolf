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

#include <Client/Renderer/ComponentRender.h>

#include <math.h>

#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Simulation.h>
#include <Shared/StaticData.h>

void rr_component_portal_render(EntityIdx entity, struct rr_game *this,
                                struct rr_simulation *simulation)
{
    struct rr_renderer *renderer = this->renderer;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);
    struct rr_component_portal *portal =
        rr_simulation_get_portal(simulation, entity);
    uint32_t color = RR_RARITY_COLORS[portal->rarity];
    float radius = physical->lerp_radius;
    float spin = physical->animation_timer * 2;

    rr_renderer_set_fill(renderer, color);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_line_width(renderer, radius * 0.08f);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 0, radius);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);

    renderer->state.filter.color = 0xffffffff;
    renderer->state.filter.amount = 0.35f;
    for (uint8_t i = 0; i < 3; ++i)
    {
        rr_renderer_begin_path(renderer);
        rr_renderer_partial_arc(renderer, 0, 0, radius * 0.65f,
                                spin + i * 2.0f * M_PI / 3,
                                spin + i * 2.0f * M_PI / 3 + M_PI / 3, 0);
        rr_renderer_stroke(renderer);
    }
    renderer->state.filter.amount = 0;

    rr_renderer_set_text_baseline(renderer, 1);
    rr_renderer_set_text_align(renderer, 1);
    rr_renderer_set_fill(renderer, 0xffffffff);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_text_size(renderer, 20);
    rr_renderer_set_line_width(renderer, 20 * 0.12f);
    rr_renderer_begin_path(renderer);
    rr_renderer_stroke_text(renderer, portal->target_dimension, 0,
                            radius + 24);
    rr_renderer_fill_text(renderer, portal->target_dimension, 0, radius + 24);
}
