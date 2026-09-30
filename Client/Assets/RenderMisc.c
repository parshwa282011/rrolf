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

#include <Client/Assets/RenderFunctions.h>

#include <math.h>
#include <stdlib.h>

#include <Client/Assets/Render.h>
#include <Client/Renderer/Renderer.h>
#include <Shared/Utilities.h>
#include <Shared/Vector.h>

#define TILES_SIZE 3
#define PROP_SIZE 2

struct rr_renderer_spritesheet background_tiles;

static void asset_web_draw(struct rr_renderer *renderer)
{
    rr_renderer_scale(renderer, 10);
    rr_renderer_set_stroke(renderer, 0xffffffff);
    rr_renderer_set_line_cap(renderer, 1);
    rr_renderer_set_line_width(renderer, 0.75);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 10, 0);
    rr_renderer_line_to(renderer, -10, 0);
    rr_renderer_move_to(renderer, 0, 10);
    rr_renderer_line_to(renderer, 0, -10);
    rr_renderer_move_to(renderer, 7.0710678118654755, 7.0710678118654755);
    rr_renderer_line_to(renderer, -7.0710678118654755, -7.0710678118654755);
    rr_renderer_move_to(renderer, -7.0710678118654755, 7.0710678118654755);
    rr_renderer_line_to(renderer, 7.0710678118654755, -7.0710678118654755);
    rr_renderer_move_to(renderer, 8, 0);
    rr_renderer_quadratic_curve_to(renderer, 4.619397662556434,
                                   1.913417161825449, 5.656854249492381,
                                   5.65685424949238);
    rr_renderer_quadratic_curve_to(renderer, 1.9134171618254492,
                                   4.619397662556434, 4.898587196589413e-16, 8);
    rr_renderer_quadratic_curve_to(renderer, -1.9134171618254485,
                                   4.619397662556434, -5.65685424949238,
                                   5.656854249492381);
    rr_renderer_quadratic_curve_to(renderer, -4.619397662556434,
                                   1.9134171618254494, -8,
                                   9.797174393178826e-16);
    rr_renderer_quadratic_curve_to(renderer, -4.619397662556434,
                                   -1.9134171618254483, -5.6568542494923815,
                                   -5.65685424949238);
    rr_renderer_quadratic_curve_to(renderer, -1.9134171618254516,
                                   -4.619397662556432, -1.4695761589768238e-15,
                                   -8);
    rr_renderer_quadratic_curve_to(renderer, 1.91341716182545,
                                   -4.619397662556433, 5.656854249492379,
                                   -5.6568542494923815);
    rr_renderer_quadratic_curve_to(renderer, 4.619397662556432,
                                   -1.913417161825452, 8,
                                   -1.959434878635765e-15);
    rr_renderer_move_to(renderer, 5, 0);
    rr_renderer_quadratic_curve_to(renderer, 2.77163859753386,
                                   1.1480502970952693, 3.5355339059327378,
                                   3.5355339059327373);
    rr_renderer_quadratic_curve_to(renderer, 1.1480502970952695,
                                   2.77163859753386, 3.061616997868383e-16, 5);
    rr_renderer_quadratic_curve_to(renderer, -1.1480502970952693,
                                   2.77163859753386, -3.5355339059327373,
                                   3.5355339059327378);
    rr_renderer_quadratic_curve_to(renderer, -2.77163859753386,
                                   1.1480502970952697, -5,
                                   6.123233995736766e-16);
    rr_renderer_quadratic_curve_to(renderer, -2.7716385975338604,
                                   -1.148050297095269, -3.5355339059327386,
                                   -3.5355339059327373);
    rr_renderer_quadratic_curve_to(renderer, -1.148050297095271,
                                   -2.7716385975338595, -9.184850993605148e-16,
                                   -5);
    rr_renderer_quadratic_curve_to(renderer, 1.14805029709527,
                                   -2.77163859753386, 3.535533905932737,
                                   -3.5355339059327386);
    rr_renderer_quadratic_curve_to(renderer, 2.7716385975338595,
                                   -1.148050297095271, 5,
                                   -1.2246467991473533e-15);
    rr_renderer_stroke(renderer);
    rr_renderer_scale(renderer, 0.1);
}

static void asset_nest_draw(struct rr_renderer *renderer)
{
    rr_renderer_scale(renderer, 250);
    rr_renderer_set_fill(renderer, 0xff4e270b);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 0, 1);
    rr_renderer_fill(renderer);
    for (uint8_t i = 0; i < 100; ++i)
    {
        struct rr_renderer_context_state state;
        rr_renderer_context_state_init(renderer, &state);
        struct rr_vector vector;
        rr_vector_from_polar(&vector, 0.9 + 0.2 * rr_frand(),
                             -2 * M_PI * i / 100 + M_PI + 0.2 * rr_frand() - 0.1);
        rr_renderer_translate(renderer, vector.x, vector.y);
        rr_renderer_rotate(renderer, -2 * M_PI * i / 100 + 0.2 * rr_frand() - 0.1);
        rr_renderer_scale(renderer, 0.03);
        rr_renderer_draw_nest_stick(renderer);
        rr_renderer_context_state_free(renderer, &state);
    }
}

void rr_renderer_draw_tile_hell_creek(struct rr_renderer *renderer, uint8_t pos)
{
    render_sprite_from_cache(renderer, &background_tiles, pos);
}

void rr_renderer_draw_tile_garden(struct rr_renderer *renderer, uint8_t pos)
{
    render_sprite_from_cache(renderer, &background_tiles, TILES_SIZE + pos);
}

void rr_renderer_draw_tile_beehive(struct rr_renderer *renderer, uint8_t pos)
{
    render_sprite_from_cache(renderer, &background_tiles,
                             2 * TILES_SIZE + pos);
}

void rr_renderer_draw_prop(struct rr_renderer *renderer, uint8_t pos)
{
    render_sprite_from_cache(renderer, &background_tiles, 3 * TILES_SIZE + pos);
}

void rr_renderer_draw_web(struct rr_renderer *renderer)
{
    render_sprite_from_cache(renderer, &background_tiles,
                             3 * TILES_SIZE + PROP_SIZE);
}

void rr_renderer_draw_nest(struct rr_renderer *renderer)
{
    render_sprite_from_cache(renderer, &background_tiles,
                             3 * TILES_SIZE + PROP_SIZE + 1);
}

void rr_renderer_draw_nest_stick(struct rr_renderer *renderer)
{
    struct rr_renderer_context_state state;
    rr_renderer_context_state_init(renderer, &state);
    rr_renderer_translate(renderer, -303, -123);
    rr_renderer_set_fill(renderer, 0xff582601);
    rr_renderer_set_stroke(renderer, 0xff000000);
    rr_renderer_set_line_width(renderer, 1);
    rr_renderer_set_line_join(renderer, 1);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 302.901, 137.729);
    rr_renderer_bezier_curve_to(renderer, 301.686, 137.729, 300.701, 136.745, 300.701, 135.531);
    rr_renderer_line_to(renderer, 300.701, 120.008);
    rr_renderer_line_to(renderer, 295.975, 111.379);
    rr_renderer_bezier_curve_to(renderer, 295.403, 110.335, 295.786, 109.024, 296.831, 108.452);
    rr_renderer_bezier_curve_to(renderer, 297.876, 107.881, 299.185, 108.264, 299.756, 109.308);
    rr_renderer_line_to(renderer, 303.517, 116.171);
    rr_renderer_line_to(renderer, 305.988, 111.74);
    rr_renderer_bezier_curve_to(renderer, 306.596, 110.648, 308.014, 110.317, 309.043, 111.028);
    rr_renderer_bezier_curve_to(renderer, 309.966, 111.664, 310.234, 112.909, 309.655, 113.869);
    rr_renderer_line_to(renderer, 305.1, 121.41);
    rr_renderer_line_to(renderer, 305.1, 135.531);
    rr_renderer_bezier_curve_to(renderer, 305.1, 136.745, 304.116, 137.729, 302.901, 137.729);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
    rr_renderer_context_state_free(renderer, &state);
}

void rr_renderer_draw_third_eye(struct rr_renderer *renderer,
                                float eye_x, float eye_y)
{
    struct rr_renderer_context_state state;
    rr_renderer_context_state_init(renderer, &state);
    rr_renderer_set_fill(renderer, 0xff222222);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 1, 15);
    rr_renderer_quadratic_curve_to(renderer, 16, 0, 1, -15);
    rr_renderer_quadratic_curve_to(renderer, 0, -16, -1, -15);
    rr_renderer_quadratic_curve_to(renderer, -16, 0, -1, 15);
    rr_renderer_quadratic_curve_to(renderer, 0, 16, 1, 15);
    rr_renderer_fill(renderer);
    rr_renderer_scale(renderer, 0.8);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 1, 15);
    rr_renderer_quadratic_curve_to(renderer, 16, 0, 1, -15);
    rr_renderer_quadratic_curve_to(renderer, 0, -16, -1, -15);
    rr_renderer_quadratic_curve_to(renderer, -16, 0, -1, 15);
    rr_renderer_quadratic_curve_to(renderer, 0, 16, 1, 15);
    rr_renderer_clip(renderer);
    rr_renderer_scale(renderer, 1.25);
    rr_renderer_set_fill(renderer, 0xffffffff);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, eye_x, eye_y, 7);
    rr_renderer_fill(renderer);
    rr_renderer_context_state_free(renderer, &state);
}

void rr_renderer_tiles_init()
{
    rr_renderer_spritesheet_init(
        &background_tiles, NULL, 256, 256, rr_hc_tile_1_draw, 256, 256,
        rr_hc_tile_2_draw, 256, 256, rr_hc_tile_3_draw, 256, 256,
        rr_ga_tile_1_draw, 256, 256, rr_ga_tile_2_draw, 256, 256,
        rr_ga_tile_3_draw, 256, 256, rr_bh_tile_1_draw, 256, 256,
        rr_bh_tile_2_draw, 256, 256, rr_bh_tile_3_draw, 800, 800,
        rr_prop_fern_draw, 800, 800, rr_prop_moss_draw, 250, 250,
        asset_web_draw, 700, 700, asset_nest_draw, 0);
}
