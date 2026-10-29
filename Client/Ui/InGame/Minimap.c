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

#include <Client/Ui/Ui.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <Client/Assets/RenderFunctions.h>
#include <Client/Ctf.h>
#include <Client/Snail.h>
#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Simulation.h>
#include <Client/Ui/Engine.h>
#include <Shared/StaticData.h>

struct rr_renderer minimap;

// what the image was drawn for: the biome, and for a hive or hole which of the
// pregenerated sizes it is
static uint16_t previous_biome = 0xffff;
// pixels of the minimap image per maze cell, set when it is drawn
static float cell_px = 1;

// A map wider than this many template cells (the size of a normal map) is shown
// as a window of this size around the player. While the mouse is over the
// minimap it shows the whole map.
#define MINIMAP_WINDOW_CELLS 40

// where a world position is in the box, from its center, given the center of
// the view and how much world the box shows
static float to_box(float world, float view_center, float span, float size)
{
    return size * ((world - view_center) / span);
}

#define DRAW_MINIMAP(renderer, grid)                                           \
    if (arena->biome * 16 + arena->size_tier != previous_biome)                \
    {                                                                          \
        previous_biome = arena->biome * 16 + arena->size_tier;                 \
        float s = floorf(this->abs_width / maze_dim);                          \
        if (s < 2)                                                             \
            s = 2;                                                             \
        if (maze_dim / 2 > MINIMAP_WINDOW_CELLS)                               \
            s = 4; /* sharp enough to zoom in on */                            \
        cell_px = s;                                                           \
        rr_renderer_set_dimensions(renderer, s *maze_dim, s *maze_dim);        \
        (renderer)->state.filter.color = 0xffffffff;                           \
        for (int32_t x = 0; x < maze_dim; ++x)                                 \
            for (int32_t y = 0; y < maze_dim; ++y)                             \
            {                                                                  \
                uint8_t at = grid[y * maze_dim + x].value;                     \
                uint8_t difficulty = grid[y * maze_dim + x].difficulty / 4;    \
                if (at == 1)                                                   \
                {                                                              \
                    if (difficulty % 2 == 0)                                   \
                        (renderer)->state.filter.amount =                      \
                            (difficulty >= 4 && difficulty <= 8) ||            \
                            difficulty == 12 ? 0.3 : 0.5;                      \
                    rr_renderer_set_fill(                                      \
                        renderer, RR_RARITY_COLORS[difficulty / 2]);           \
                    rr_renderer_begin_path(renderer);                          \
                    rr_renderer_fill_rect(renderer, x *s, y *s, s, s);         \
                }                                                              \
                else if (at != 0)                                              \
                {                                                              \
                    for (int8_t i = -1; i <= 1; ++i)                           \
                        for (int8_t j = -1; j <= 1; ++j)                       \
                    {                                                          \
                        if (x + i < 0 || x + i >= maze_dim ||                  \
                            y + j < 0 || y + j >= maze_dim)                    \
                            continue;                                          \
                        uint8_t potential =                                    \
                            grid[(y + j) * maze_dim + (x + i)].difficulty / 4; \
                        if (potential > difficulty)                            \
                            difficulty = potential;                            \
                    }                                                          \
                    uint8_t left = (at >> 1) & 1;                              \
                    uint8_t top = at & 1;                                      \
                    uint8_t inverse = (at >> 3) & 1;                           \
                    if (difficulty % 2 == 0)                                   \
                        (renderer)->state.filter.amount =                      \
                            (difficulty >= 4 && difficulty <= 8) ||            \
                            difficulty == 12 ? 0.3 : 0.5;                      \
                    rr_renderer_set_fill(                                      \
                        renderer, RR_RARITY_COLORS[difficulty / 2]);           \
                    rr_renderer_begin_path(renderer);                          \
                    rr_renderer_move_to(renderer, (x + inverse ^ left) * s,    \
                                        (y + inverse ^ top) * s);              \
                    float start_angle = 0;                                     \
                    if (top == 0 && left == 1)                                 \
                        start_angle = M_PI / 2;                                \
                    else if (top == 1 && left == 1)                            \
                        start_angle = M_PI;                                    \
                    else if (top == 1 && left == 0)                            \
                        start_angle = M_PI * 3 / 2;                            \
                    rr_renderer_partial_arc(renderer, (x + left) * s,          \
                                            (y + top) * s, s, start_angle,     \
                                            start_angle + M_PI / 2, 0);        \
                    rr_renderer_fill(renderer);                                \
                }                                                              \
                (renderer)->state.filter.amount = 0;                           \
            }                                                                  \
    }

static void minimap_on_render(struct rr_ui_element *this, struct rr_game *game)
{
    if (!game->simulation_ready)
        return;
    struct rr_renderer *renderer = game->renderer;
    struct rr_component_arena *arena =
        rr_simulation_get_arena(game->simulation, game->player_info->arena);
    float grid_size;
    uint32_t maze_dim;
    struct rr_maze_grid *grid;
    if (arena->biome == rr_biome_id_beehive ||
        arena->biome == rr_biome_id_ant_hole)
    {
        struct rr_maze_declaration *tier =
            &RR_BEEHIVE_MAZE_TIERS[arena->size_tier];
        grid_size = tier->grid_size;
        maze_dim = tier->maze_dim;
        grid = tier->maze;
    }
    else
    {
        grid_size = RR_MAZES[arena->biome].grid_size;
        maze_dim = RR_MAZES[arena->biome].maze_dim;
        grid = RR_MAZES[arena->biome].maze;
    }
    DRAW_MINIMAP(&minimap, grid);
    // what part of the world the box shows
    float world = grid_size * maze_dim;
    float view_x = world / 2, view_y = world / 2, span = world;
    uint8_t zoomed = 0;
    if (maze_dim / 2 > MINIMAP_WINDOW_CELLS && !rr_ui_mouse_over(this, game))
    {
        zoomed = 1;
        span = MINIMAP_WINDOW_CELLS * 2 * grid_size;
        view_x = game->player_info->lerp_camera_x;
        view_y = game->player_info->lerp_camera_y;
    }
    rr_renderer_scale(renderer, renderer->scale);
    if (zoomed)
    {
        // the part of the image around the player, as many pixels as the box
        float window = span / grid_size * cell_px;
        rr_renderer_scale(renderer, this->abs_width / window);
        rr_renderer_draw_clipped_image(renderer, &minimap,
                                       view_x / grid_size * cell_px,
                                       view_y / grid_size * cell_px, window,
                                       window, 0, 0);
        rr_renderer_scale(renderer, window / this->abs_width);
    }
    else
    {
        rr_renderer_scale(renderer, this->abs_width / minimap.width);
        rr_renderer_draw_image(renderer, &minimap);
        rr_renderer_scale(renderer, minimap.width / this->abs_width);
    }
    rr_renderer_set_line_width(renderer, 2);
    if (arena->biome == rr_biome_id_ctf)
        rr_ctf_minimap_overlay(game, this->abs_width, view_x, view_y, span);
    if (arena->biome == rr_biome_id_garden)
        rr_snail_minimap_overlay(game, this->abs_width, view_x, view_y, span);
    if (arena->biome == rr_biome_id_pvp)
    {
        rr_renderer_set_fill(renderer, 0xffff0000);
        renderer->state.filter.amount = 0.2;
        rr_renderer_set_stroke(renderer, 0xffff0000);
        renderer->state.filter.amount = 0;
        for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
        {
            if (game->arena_player_infos[i] == RR_NULL_ENTITY)
                continue;
            struct rr_component_player_info *player_info =
                rr_simulation_get_player_info(game->simulation,
                                              game->arena_player_infos[i]);
            if (player_info->arena != game->player_info->arena)
                continue;
            if (player_info->flower_id == RR_NULL_ENTITY)
                continue;
            struct rr_component_physical *physical =
                rr_simulation_get_physical(game->simulation,
                                           player_info->flower_id);
            rr_renderer_begin_path(renderer);
            rr_renderer_arc(
                renderer,
                to_box(physical->lerp_x, view_x, span, this->abs_width),
                to_box(physical->lerp_y, view_y, span, this->abs_height), 4);
            rr_renderer_fill(renderer);
            rr_renderer_stroke(renderer);
        }
    }
    rr_renderer_set_fill(renderer, 0xffff00ff);
    renderer->state.filter.amount = 0.2;
    rr_renderer_set_stroke(renderer, 0xffff00ff);
    renderer->state.filter.amount = 0;
    for (uint32_t i = RR_SQUAD_MEMBER_COUNT; i > 0; --i)
    {
        if (game->player_infos[i - 1] == RR_NULL_ENTITY)
            continue;
        struct rr_component_player_info *player_info =
            rr_simulation_get_player_info(game->simulation,
                                          game->player_infos[i - 1]);
        if (player_info->arena != game->player_info->arena)
            continue;
        if (player_info->flower_id == RR_NULL_ENTITY)
            continue;
        struct rr_component_physical *physical =
            rr_simulation_get_physical(game->simulation,
                                       player_info->flower_id);
        if (i == 1)
        {
            rr_renderer_set_fill(renderer, 0xff0080ff);
            renderer->state.filter.amount = 0.2;
            rr_renderer_set_stroke(renderer, 0xff0080ff);
            renderer->state.filter.amount = 0;
        }
        float bx = to_box(physical->lerp_x, view_x, span, this->abs_width);
        float by = to_box(physical->lerp_y, view_y, span, this->abs_height);
        // a squadmate outside the window is not drawn on its edge
        if (fabsf(bx) > this->abs_width / 2 + 4 ||
            fabsf(by) > this->abs_height / 2 + 4)
            continue;
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, bx, by, 4);
        rr_renderer_fill(renderer);
        rr_renderer_stroke(renderer);
    }
    if (zoomed)
    {
        rr_renderer_set_fill(renderer, 0xccffffff);
        rr_renderer_set_stroke(renderer, 0xff222222);
        rr_renderer_set_text_baseline(renderer, 1);
        rr_renderer_set_text_align(renderer, 1);
        rr_renderer_set_text_size(renderer, 11);
        rr_renderer_set_line_width(renderer, 11 * 0.12f);
        rr_renderer_stroke_text(renderer, "hover for the whole map", 0,
                                this->abs_height / 2 - 9);
        rr_renderer_fill_text(renderer, "hover for the whole map", 0,
                              this->abs_height / 2 - 9);
    }
}

static void minimap_redraw(void *captures)
{
    previous_biome = 0xffff;
}

void rr_minimap_invalidate() { previous_biome = 0xffff; }

struct rr_ui_element *rr_ui_minimap_init(struct rr_game *game)
{
    struct rr_ui_element *this = rr_ui_element_init();

    this->abs_width = this->width = this->abs_height = this->height = 250;
    this->on_render = minimap_on_render;
    rr_renderer_init(&minimap);
    minimap.on_context_restore = minimap_redraw;
    return rr_ui_set_justify(this, 1, -1);
}