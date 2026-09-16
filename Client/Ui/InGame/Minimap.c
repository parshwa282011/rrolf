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

#include <Client/Ui/Ui.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <Client/Assets/RenderFunctions.h>
#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Simulation.h>
#include <Shared/StaticData.h>

struct rr_renderer minimap;

// by default the minimap only shows an RR_MINIMAP_WINDOW_DIM x
// RR_MINIMAP_WINDOW_DIM window of maze cells centered on the player -
// cheap enough to redraw whenever that window shifts. hovering the
// minimap instead shows the whole maze (see minimap_on_render). drawing
// the whole maze (up to maze_dim^2 canvas calls) in a single frame is
// what used to freeze the client for a moment on the med map (240x240 =
// 9x the cells of the old 80x80 map) the instant a player's biome
// resolved, so whichever view is active fills in over a bounded number
// of rows per frame rather than blocking one.
#define RR_MINIMAP_WINDOW_DIM 80
#define RR_MINIMAP_CELLS_PER_FRAME (6400)

static uint8_t previous_biome = 255;
static int32_t minimap_draw_row = -1; // next local row to draw, -1 = idle
static int32_t minimap_view_dim = 0;  // width/height of the active view
static int32_t minimap_origin_x = 0;  // top-left of the active view, in
static int32_t minimap_origin_y = 0;  // absolute maze grid coordinates
static float minimap_cell_size = 0;

static void minimap_draw_cell(struct rr_renderer *renderer,
                              struct rr_maze_grid *grid, int32_t maze_dim,
                              int32_t grid_x, int32_t grid_y,
                              int32_t canvas_x, int32_t canvas_y, float s)
{
    uint8_t at = grid[grid_y * maze_dim + grid_x].value;
    uint8_t difficulty = grid[grid_y * maze_dim + grid_x].difficulty / 4;
    if (at == 1)
    {
        if (difficulty % 2 == 0)
            renderer->state.filter.amount =
                (difficulty >= 4 && difficulty <= 8) || difficulty == 12
                    ? 0.3
                    : 0.5;
        rr_renderer_set_fill(renderer, RR_RARITY_COLORS[difficulty / 2]);
        rr_renderer_begin_path(renderer);
        rr_renderer_fill_rect(renderer, canvas_x * s, canvas_y * s, s, s);
    }
    else if (at != 0)
    {
        for (int8_t i = -1; i <= 1; ++i)
            for (int8_t j = -1; j <= 1; ++j)
            {
                if (grid_x + i < 0 || grid_x + i >= maze_dim ||
                    grid_y + j < 0 || grid_y + j >= maze_dim)
                    continue;
                uint8_t potential =
                    grid[(grid_y + j) * maze_dim + (grid_x + i)].difficulty /
                    4;
                if (potential > difficulty)
                    difficulty = potential;
            }
        uint8_t left = (at >> 1) & 1;
        uint8_t top = at & 1;
        uint8_t inverse = (at >> 3) & 1;
        if (difficulty % 2 == 0)
            renderer->state.filter.amount =
                (difficulty >= 4 && difficulty <= 8) || difficulty == 12
                    ? 0.3
                    : 0.5;
        rr_renderer_set_fill(renderer, RR_RARITY_COLORS[difficulty / 2]);
        rr_renderer_begin_path(renderer);
        rr_renderer_move_to(renderer, (canvas_x + inverse ^ left) * s,
                            (canvas_y + inverse ^ top) * s);
        float start_angle = 0;
        if (top == 0 && left == 1)
            start_angle = M_PI / 2;
        else if (top == 1 && left == 1)
            start_angle = M_PI;
        else if (top == 1 && left == 0)
            start_angle = M_PI * 3 / 2;
        rr_renderer_partial_arc(renderer, (canvas_x + left) * s,
                                (canvas_y + top) * s, s, start_angle,
                                start_angle + M_PI / 2, 0);
        rr_renderer_fill(renderer);
    }
    renderer->state.filter.amount = 0;
}

// call once per frame. resumes drawing wherever the last call left off, so
// the active view (window or whole map) fills in over a handful of
// frames instead of blocking one.
static void minimap_draw_step(struct rr_renderer *renderer,
                              struct rr_maze_grid *grid, int32_t maze_dim)
{
    if (minimap_draw_row < 0)
        return;
    int32_t rows_per_frame = RR_MINIMAP_CELLS_PER_FRAME / minimap_view_dim;
    if (rows_per_frame < 1)
        rows_per_frame = 1;
    int32_t end_row = minimap_draw_row + rows_per_frame;
    if (end_row > minimap_view_dim)
        end_row = minimap_view_dim;
    for (int32_t lx = minimap_draw_row; lx < end_row; ++lx)
        for (int32_t ly = 0; ly < minimap_view_dim; ++ly)
            minimap_draw_cell(renderer, grid, maze_dim,
                              minimap_origin_x + lx, minimap_origin_y + ly,
                              lx, ly, minimap_cell_size);
    minimap_draw_row = end_row >= minimap_view_dim ? -1 : end_row;
}

static void minimap_on_render(struct rr_ui_element *this, struct rr_game *game)
{
    if (!game->simulation_ready)
        return;
    struct rr_renderer *renderer = game->renderer;
    struct rr_component_player_info *player_info = game->player_info;
    float a = renderer->height / 1080;
    float b = renderer->width / 1920;

    float s1 = (renderer->scale = b < a ? a : b);
    double scale = player_info->lerp_camera_fov * 0.25;
    struct rr_component_arena *arena =
        rr_simulation_get_arena(game->simulation, player_info->arena);
    float grid_size = RR_MAZES[arena->biome].grid_size;
    uint32_t maze_dim = RR_MAZES[arena->biome].maze_dim;
    struct rr_maze_grid *grid = RR_MAZES[arena->biome].maze;

    uint8_t hovering = rr_ui_mouse_over(this, game);
    int32_t view_dim =
        hovering || (int32_t)maze_dim < RR_MINIMAP_WINDOW_DIM
            ? (int32_t)maze_dim
            : RR_MINIMAP_WINDOW_DIM;
    int32_t origin_x = 0, origin_y = 0;
    if (!hovering)
    {
        origin_x =
            (int32_t)(player_info->lerp_camera_x / grid_size) - view_dim / 2;
        origin_y =
            (int32_t)(player_info->lerp_camera_y / grid_size) - view_dim / 2;
        origin_x = rr_fclamp(origin_x, 0, (int32_t)maze_dim - view_dim);
        origin_y = rr_fclamp(origin_y, 0, (int32_t)maze_dim - view_dim);
    }
    if (arena->biome != previous_biome || view_dim != minimap_view_dim ||
        origin_x != minimap_origin_x || origin_y != minimap_origin_y)
    {
        previous_biome = arena->biome;
        minimap_view_dim = view_dim;
        minimap_origin_x = origin_x;
        minimap_origin_y = origin_y;
        minimap_cell_size = floorf(this->abs_width / view_dim);
        if (minimap_cell_size < 1)
            minimap_cell_size = 1;
        rr_renderer_set_dimensions(&minimap, minimap_cell_size * view_dim,
                                   minimap_cell_size * view_dim);
        minimap.state.filter.color = 0xffffffff;
        minimap_draw_row = 0;
    }
    minimap_draw_step(&minimap, grid, maze_dim);

    double view_world_size = grid_size * minimap_view_dim;
    double midX = ((player_info->lerp_camera_x / grid_size -
                   minimap_origin_x) /
                      minimap_view_dim -
                  0.5) *
                  this->abs_width;
    double midY = ((player_info->lerp_camera_y / grid_size -
                   minimap_origin_y) /
                      minimap_view_dim -
                  0.5) *
                  this->abs_height;
    double W = renderer->width / scale / view_world_size * this->abs_width;
    double H = renderer->height / scale / view_world_size * this->abs_height;
    rr_renderer_scale(renderer, renderer->scale);
    rr_renderer_begin_path(renderer);
    rr_renderer_rect(renderer, midX - W / 2, midY - H / 2, W, H);
    // rr_renderer_clip(renderer);
    rr_renderer_scale(renderer, this->abs_width / minimap.width);
    rr_renderer_draw_image(renderer, &minimap);
    rr_renderer_scale(renderer, minimap.width / this->abs_width);
    rr_renderer_set_fill(renderer, 0xff0000ff);
    rr_renderer_set_global_alpha(renderer, 0.8);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, midX, midY, 2);
    rr_renderer_fill(renderer);
    rr_renderer_set_fill(renderer, 0xffff00ff);
    for (uint32_t i = 1; i < RR_SQUAD_MEMBER_COUNT; ++i)
    {
        if (game->player_infos[i] == RR_NULL_ENTITY)
            break;
        player_info = rr_simulation_get_player_info(game->simulation,
                                                    game->player_infos[i]);
        if (player_info->arena != game->player_info->arena)
            continue;
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer,
                        this->abs_width *
                            ((player_info->camera_x / grid_size -
                              minimap_origin_x) /
                                 minimap_view_dim -
                             0.5),
                        this->abs_height *
                            ((player_info->camera_y / grid_size -
                              minimap_origin_y) /
                                 minimap_view_dim -
                             0.5),
                        2);
        rr_renderer_fill(renderer);
    }
}

struct rr_ui_element *rr_ui_minimap_init(struct rr_game *game)
{
    struct rr_ui_element *this = rr_ui_element_init();

    this->abs_width = this->width = this->abs_height = this->height = 200;
    this->on_render = minimap_on_render;
    rr_renderer_init(&minimap);
    return this;
}