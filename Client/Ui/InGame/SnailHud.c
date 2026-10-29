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
#include <string.h>

#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Snail.h>
#include <Client/Ui/Engine.h>
#include <Shared/Snail.h>
#include <Shared/StaticData.h>

// The snail run banner at the top of the screen: what is going on, how far the
// snail is and how it is doing. Under it the latest lines of the feed

#define HUD_WIDTH 420
#define HUD_HEIGHT 120

static void text(struct rr_renderer *renderer, char const *string, float x,
                 float y, float size, uint32_t color)
{
    rr_renderer_set_fill(renderer, color);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_text_baseline(renderer, 1);
    rr_renderer_set_text_align(renderer, 1);
    rr_renderer_set_text_size(renderer, size);
    rr_renderer_set_line_width(renderer, size * 0.12f);
    rr_renderer_stroke_text(renderer, string, x, y);
    rr_renderer_fill_text(renderer, string, x, y);
}

static void bar(struct rr_renderer *renderer, float y, float width,
                float height, float fraction, uint32_t color)
{
    rr_renderer_set_fill(renderer, 0xff222222);
    rr_renderer_begin_path(renderer);
    rr_renderer_round_rect(renderer, -width / 2, y, width, height, height / 2);
    rr_renderer_fill(renderer);
    if (fraction <= 0)
        return;
    rr_renderer_set_fill(renderer, color);
    rr_renderer_begin_path(renderer);
    rr_renderer_round_rect(renderer, -width / 2 + 2, y + 2,
                           (width - 4) * fraction, height - 4,
                           (height - 4) / 2);
    rr_renderer_fill(renderer);
}

static uint8_t hud_should_show(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_game_snail *s = &game->snail;
    if (game->cache.hide_ui || !game->simulation_ready || !s->active)
        return 0;
    if (s->run_count > 0)
        return 1;
    // after a run, the latest lines for a while
    for (uint8_t i = 0; i < 3; ++i)
        if (s->feed[i].text[0] != 0 && s->feed[i].age < 8)
            return 1;
    return 0;
}

static void hud_on_render(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_game_snail *s = &game->snail;
    struct rr_renderer *renderer = game->renderer;
    rr_renderer_scale(renderer, renderer->scale);
    rr_renderer_translate(renderer, 0, -this->abs_height / 2);
    float y = 0;
    struct rr_game_snail_run *run = rr_snail_client_focus(game);
    if (run != NULL)
    {
        char title[64];
        // more than one snail (the dev panel can start several)
        char more[32] = "";
        if (s->run_count > 1)
            snprintf(more, sizeof more, "  (%u snails)", s->run_count);
        if (run->waiting)
        {
            // it does not move until somebody is close, the minimap shows where
            snprintf(title, sizeof title, "Find the %s snail!%s",
                     RR_RARITY_NAMES[run->rarity], more);
            text(renderer, title, 0, 12, 20, RR_RARITY_COLORS[run->rarity]);
            text(renderer, "It waits where the marker is on the minimap", 0, 40,
                 13, 0xffdddddd);
            text(renderer, "Get close and it sets off for the hole", 0, 60, 13,
                 0xffbbbbbb);
            y = 76;
            goto feed;
        }
        snprintf(title, sizeof title, "Protect the %s snail!%s",
                 RR_RARITY_NAMES[run->rarity], more);
        text(renderer, title, 0, 12, 20, RR_RARITY_COLORS[run->rarity]);
        float progress = run->route_cells == 0
                             ? 0
                             : 1 - (float)run->cells_left / run->route_cells;
        text(renderer, "Home", -HUD_WIDTH / 2 + 18, 38, 12, 0xffdddddd);
        bar(renderer, 46, HUD_WIDTH - 60, 10, rr_fclamp(progress, 0, 1),
            0xff7ee0b0);
        text(renderer, "Health", -HUD_WIDTH / 2 + 22, 68, 12, 0xffdddddd);
        bar(renderer, 76, HUD_WIDTH - 60, 10, run->hp / 100.0f, 0xff75dd34);
        text(renderer, "Stay close, Mint heals it", 0, 100, 12, 0xffbbbbbb);
        y = 118;
    }
feed:
    for (uint8_t i = 0; i < 3; ++i)
    {
        if (s->feed[i].text[0] == 0 || s->feed[i].age >= 8)
            continue;
        float fade = s->feed[i].age > 6 ? (8 - s->feed[i].age) / 2 : 1;
        rr_renderer_set_global_alpha(renderer, fade);
        text(renderer, s->feed[i].text, 0, y + 9, 15, s->feed[i].color);
        rr_renderer_set_global_alpha(renderer, 1);
        y += 20;
    }
}

struct rr_ui_element *rr_ui_snail_hud_init(struct rr_game *game)
{
    struct rr_ui_element *this = rr_ui_element_init();
    this->abs_width = this->width = HUD_WIDTH;
    this->abs_height = this->height = HUD_HEIGHT;
    this->on_render = hud_on_render;
    this->should_show = hud_should_show;
    this->animate = rr_ui_instant_hide_animate;
    return rr_ui_set_justify(this, 0, -1);
}
