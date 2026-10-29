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

#include <math.h>

#include <Client/Assets/RenderFunctions.h>
#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Simulation.h>

void rr_component_flower_render(EntityIdx entity, struct rr_game *game,
                                struct rr_simulation *simulation)
{
    struct rr_renderer *renderer = game->renderer;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);
    struct rr_component_flower *flower =
        rr_simulation_get_flower(simulation, entity);
    struct rr_renderer_context_state state;
    rr_renderer_add_color_filter(
        renderer, 0xffff0000,
        0.5 * rr_simulation_get_health(simulation, entity)->damage_animation);
    rr_renderer_set_global_alpha(renderer, (1 - physical->deletion_animation) *
                                               renderer->state.global_alpha);
    rr_renderer_scale(renderer, 1 + physical->deletion_animation * 0.5);
    rr_renderer_scale(renderer, physical->radius / 25);
    rr_renderer_rotate(renderer, physical->lerp_angle);
    rr_renderer_set_stroke(renderer, 0xffcfbb50);
    rr_renderer_set_fill(renderer, 0xffffe763);
    rr_renderer_set_line_width(renderer, 3);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 0, 25);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_line_width(renderer, 1.5);
    rr_renderer_set_line_cap(renderer, 1);
    if (flower->dead)
    {
        rr_renderer_begin_path(renderer);
        rr_renderer_move_to(renderer, -10, -8);
        rr_renderer_line_to(renderer, -4, -2);
        rr_renderer_move_to(renderer, -4, -8);
        rr_renderer_line_to(renderer, -10, -2);
        rr_renderer_move_to(renderer, 10, -8);
        rr_renderer_line_to(renderer, 4, -2);
        rr_renderer_move_to(renderer, 4, -8);
        rr_renderer_line_to(renderer, 10, -2);
        rr_renderer_stroke(renderer);
    }
    else
    {
        rr_renderer_context_state_init(renderer, &state);
        rr_renderer_set_fill(renderer, 0xff222222);
        rr_renderer_begin_path(renderer);
        rr_renderer_ellipse(renderer, -7, -5, 3.25, 6.5);
        rr_renderer_fill(renderer);
        rr_renderer_begin_path(renderer);
        rr_renderer_ellipse(renderer, 7, -5, 3.25, 6.5);
        rr_renderer_fill(renderer);
        rr_renderer_begin_path(renderer);
        rr_renderer_ellipse(renderer, -7, -5, 3, 6);
        rr_renderer_ellipse(renderer, 7, -5, 3, 6);
        rr_renderer_clip(renderer);
        rr_renderer_set_fill(renderer, 0xffffffff);
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, -7 + flower->lerp_eye_x, -5 + flower->lerp_eye_y,
                        3);
        rr_renderer_fill(renderer);
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, 7 + flower->lerp_eye_x, -5 + flower->lerp_eye_y,
                        3);
        rr_renderer_fill(renderer);
        rr_renderer_context_state_free(renderer, &state);
    }
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -6, 10);
    rr_renderer_quadratic_curve_to(renderer, 0, flower->lerp_mouth, 6, 10);
    rr_renderer_stroke(renderer);
    if (flower->lerp_mouth <= 8 && !flower->dead &&
        renderer->state.global_alpha > 0.5)
    {
        rr_renderer_context_state_init(renderer, &state);
        rr_renderer_translate(renderer, 0, -flower->lerp_mouth - 7.8);
        rr_renderer_set_fill(renderer, 0xffffe763);
        rr_renderer_begin_path(renderer);
        rr_renderer_move_to(renderer, -12, 0);
        rr_renderer_line_to(renderer, 12, 0);
        rr_renderer_line_to(renderer, 0, 6);
        rr_renderer_line_to(renderer, -12, 0);
        rr_renderer_fill(renderer);
        rr_renderer_context_state_free(renderer, &state);
    }
    if (flower->crest_count)
    {
        rr_renderer_context_state_init(renderer, &state);
        rr_renderer_translate(renderer, 0, -21.75);
        rr_renderer_scale2(renderer, 1, flower->crest_count);
        rr_renderer_translate(renderer, 0, -14.25);
        rr_renderer_draw_petal(renderer, rr_petal_id_crest, 0);
        rr_renderer_context_state_free(renderer, &state);
    }
    if (flower->third_eye_count)
    {
        float start = ((flower->third_eye_count - 1) / 2 * 2 + (flower->third_eye_count - 1) % 2) / (2 * (float)RR_MAX_SLOT_COUNT);
        struct rr_vector vector;
        for (uint8_t i = 0; i < flower->third_eye_count; ++i)
        {
            rr_vector_from_polar(&vector, 1, 2 * M_PI * (-0.25 - start + i / (float)RR_MAX_SLOT_COUNT));
            rr_vector_scale(&vector, 15 + 4 * fabsf(vector.x) + 3 * fmaxf(vector.y, 0));
            rr_renderer_context_state_init(renderer, &state);
            rr_renderer_translate(renderer, vector.x, vector.y);
            if (flower->dead)
            {
                rr_renderer_begin_path(renderer);
                rr_renderer_move_to(renderer, 2, -2);
                rr_renderer_line_to(renderer, -2, 2);
                rr_renderer_move_to(renderer, -2, -2);
                rr_renderer_line_to(renderer, 2, 2);
                rr_renderer_stroke(renderer);
            }
            else
            {
                rr_renderer_scale(renderer, 0.375);
                rr_renderer_draw_third_eye(
                    renderer, flower->lerp_eye_x, flower->lerp_eye_y);
            }
            rr_renderer_context_state_free(renderer, &state);
        }
    }
    // one small eye below the flower for every hit a nazar still blocks
    if (flower->nazar_charges && !flower->dead)
    {
        float width = 9.0f;
        float start = -(flower->nazar_charges - 1) * width / 2;
        for (uint8_t i = 0; i < flower->nazar_charges; ++i)
        {
            rr_renderer_context_state_init(renderer, &state);
            rr_renderer_translate(renderer, start + i * width, 29);
            rr_renderer_scale(renderer, 0.42f);
            rr_renderer_draw_petal(renderer, rr_petal_id_nazar, 0);
            rr_renderer_context_state_free(renderer, &state);
        }
    }
}
