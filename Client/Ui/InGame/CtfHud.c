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
#include <string.h>

#include <Client/Ctf.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Ctf.h>

// The capture the flag screen: the phase and its timer at the top, a row for
// every team, the latest events. Drawn on the menu while a match is set up and
// in the game.

#define HUD_WIDTH 500
#define HUD_HEIGHT 250

static void text(struct rr_renderer *renderer, char const *string, float x,
                 float y, float size, uint32_t color, uint8_t align)
{
    rr_renderer_set_fill(renderer, color);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_text_baseline(renderer, 1);
    rr_renderer_set_text_align(renderer, align);
    rr_renderer_set_text_size(renderer, size);
    rr_renderer_set_line_width(renderer, size * 0.12f);
    rr_renderer_stroke_text(renderer, string, x, y);
    rr_renderer_fill_text(renderer, string, x, y);
}

static uint8_t hud_should_show(struct rr_ui_element *this,
                               struct rr_game *game)
{
    struct rr_game_ctf *c = &game->ctf;
    if (game->cache.hide_ui || !rr_ctf_client_selected(game) || !c->active)
        return 0;
    if (game->simulation_ready)
        return 1;
    // on the menu only when there is something to say
    return c->team_count > 0 || c->message != rr_ctf_message_none;
}

static uint32_t seconds_left(struct rr_game_ctf *c)
{
    return (c->phase_ticks + RR_CTF_TPS - 1) / RR_CTF_TPS;
}

static void hud_on_render(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_game_ctf *c = &game->ctf;
    struct rr_renderer *renderer = game->renderer;
    rr_renderer_scale(renderer, renderer->scale);
    rr_renderer_translate(renderer, 0, -this->abs_height / 2);

    char title[96] = "";
    char sub[160] = "";
    uint32_t sub_color = 0xffdddddd;
    char clock[16];
    uint8_t mine = c->my_team < RR_CTF_MAX_TEAMS && c->has_body;
    switch (c->phase)
    {
    case rr_ctf_phase_lobby:
        if (c->team_count == 0)
        {
            snprintf(title, sizeof title, "Capture the Flag");
            snprintf(sub, sizeof sub,
                     "The host links the squads from the flag button");
        }
        else
        {
            snprintf(title, sizeof title, "Lobby: %u squads linked",
                     c->team_count);
            snprintf(sub, sizeof sub,
                     c->is_host ? "Press Start in the host panel when ready"
                                : "Waiting for the host to start");
        }
        break;
    case rr_ctf_phase_placement:
        rr_ctf_client_format_time(clock, sizeof clock, seconds_left(c));
        snprintf(title, sizeof title, "Place your flags  %s", clock);
        if (c->is_leader && mine)
        {
            uint8_t placed = 0;
            for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
                placed += c->teams[c->my_team].point_set[i];
            snprintf(sub, sizeof sub,
                     placed < RR_CTF_FLAGS_PER_TEAM
                         ? "Press F where you stand to place flag %u of 3"
                         : "All placed. F moves them, or wait for the rest",
                     placed < RR_CTF_FLAGS_PER_TEAM ? placed + 1 : 0);
        }
        else
            snprintf(sub, sizeof sub, "Your squad leader places the flags");
        break;
    case rr_ctf_phase_farm:
        rr_ctf_client_format_time(clock, sizeof clock, seconds_left(c));
        snprintf(title, sizeof title, "Walls drop in %s", clock);
        snprintf(sub, sizeof sub,
                 "Farm mobs, craft [C], and set your loadout [Z]");
        break;
    case rr_ctf_phase_war:
        rr_ctf_client_format_time(clock, sizeof clock,
                                  c->phase_ticks / RR_CTF_TPS);
        snprintf(title, sizeof title, "WAR  %s", clock);
        snprintf(sub, sizeof sub,
                 "Take their flags to your flag points. Keep 3 or you are out");
        break;
    case rr_ctf_phase_ended:
        snprintf(title, sizeof title, "%s wins!",
                 c->winner < RR_CTF_MAX_TEAMS ? rr_ctf_team_name(c->winner)
                                              : "Nobody");
        rr_ctf_client_format_time(clock, sizeof clock, seconds_left(c));
        snprintf(sub, sizeof sub, "Back to the lobby in %s", clock);
        break;
    }
    if (mine && c->teams[c->my_team].eliminated &&
        c->phase != rr_ctf_phase_ended)
    {
        snprintf(sub, sizeof sub,
                 "Your team is out. Leave with the X, or watch your team");
        sub_color = 0xffff7070;
    }
    char message[96];
    if (rr_ctf_client_message_text(game, message, sizeof message) != NULL)
    {
        snprintf(sub, sizeof sub, "%s", message);
        sub_color = 0xffffd060;
    }

    // how tall it has to be
    float y = 8;
    uint8_t rows = c->has_body ? c->team_count : 0;
    uint8_t lines = 0;
    for (uint8_t i = 0; i < 5; ++i)
        if (c->feed[i].age < 8)
            ++lines;
    float height = 70 + rows * 24 + (lines ? 8 + lines * 18 : 0);
    rr_renderer_set_fill(renderer, 0x70000000);
    rr_renderer_begin_path(renderer);
    rr_renderer_round_rect(renderer, -HUD_WIDTH / 2, 0, HUD_WIDTH, height, 10);
    rr_renderer_fill(renderer);

    uint32_t title_color = 0xffffffff;
    if (c->phase == rr_ctf_phase_war)
        title_color = 0xffff8a70;
    else if (c->phase == rr_ctf_phase_ended && c->winner < RR_CTF_MAX_TEAMS)
        title_color = rr_ctf_team_color(c->winner);
    text(renderer, title, 0, y + 14, 28, title_color, 1);
    y += 40;
    text(renderer, sub, 0, y + 6, 15, sub_color, 1);
    y += 28;
    for (uint8_t t = 0; t < rows; ++t)
    {
        struct rr_game_ctf_team *team = &c->teams[t];
        uint32_t color = rr_ctf_team_color(t);
        rr_renderer_set_fill(renderer, team->eliminated ? 0xff555555 : color);
        rr_renderer_set_stroke(renderer, 0xff222222);
        rr_renderer_set_line_width(renderer, 3);
        rr_renderer_begin_path(renderer);
        rr_renderer_round_rect(renderer, -HUD_WIDTH / 2 + 16, y + 3, 16, 16, 4);
        rr_renderer_fill(renderer);
        rr_renderer_stroke(renderer);
        char name[48];
        snprintf(name, sizeof name, "%s%s", rr_ctf_team_name(t),
                 t == c->my_team ? " (you)" : "");
        text(renderer, name, -HUD_WIDTH / 2 + 42, y + 11, 16,
             team->eliminated ? 0xff999999 : 0xffffffff, 0);
        char status[64];
        if (team->eliminated)
            snprintf(status, sizeof status, "OUT");
        else if (c->flags_ready)
            snprintf(status, sizeof status, "flags %u    banked %u    %u players",
                     team->held, team->banked, team->members);
        else
            snprintf(status, sizeof status, "%u players", team->members);
        text(renderer, status, HUD_WIDTH / 2 - 16, y + 11, 15,
             team->eliminated ? 0xffff6060 : 0xffdddddd, 2);
        y += 24;
    }
    if (lines)
    {
        y += 8;
        for (uint8_t i = 0; i < 5; ++i)
        {
            if (c->feed[i].age >= 8)
                continue;
            float fade = c->feed[i].age > 6 ? (8 - c->feed[i].age) / 2 : 1;
            rr_renderer_set_global_alpha(renderer, fade);
            text(renderer, c->feed[i].text, 0, y + 9, 14, c->feed[i].color, 1);
            rr_renderer_set_global_alpha(renderer, 1);
            y += 18;
        }
    }
}

struct rr_ui_element *rr_ui_ctf_hud_init(struct rr_game *game)
{
    struct rr_ui_element *this = rr_ui_element_init();
    this->abs_width = this->width = HUD_WIDTH;
    this->abs_height = this->height = HUD_HEIGHT;
    this->on_render = hud_on_render;
    this->should_show = hud_should_show;
    this->animate = rr_ui_instant_hide_animate;
    return rr_ui_set_justify(this, 0, -1);
}

// ---- the loadout in the game

// the loadout and the inventory can be changed during a match, with the
// inventory or crafting menu open
static uint8_t loadout_should_show(struct rr_ui_element *this,
                                   struct rr_game *game)
{
    return rr_ctf_client_in_game(game) && !game->cache.hide_ui &&
           (game->menu_open == rr_game_menu_inventory ||
            game->menu_open == rr_game_menu_crafting);
}

static void respawn_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    data->clickable = game->socket_ready;
    if (!game->socket_ready)
        return;
    if (game->input_data->mouse_buttons_up_this_tick & 1 &&
        game->pressed == this)
        rr_ctf_client_send_respawn(game);
    game->cursor = rr_game_cursor_pointer;
}

struct rr_ui_element *rr_ui_ctf_loadout_init(struct rr_game *game)
{
    struct rr_ui_element *respawn =
        rr_ui_labeled_button_init("Respawn with this loadout", 24, NULL);
    respawn->on_event = respawn_on_event;
    rr_ui_set_background(respawn, 0x80d4b30c);
    return rr_ui_link_toggle(
        rr_ui_set_justify(
            rr_ui_v_container_init(
                rr_ui_container_init(), 15, 15,
                rr_ui_h_container_init(
                    rr_ui_container_init(), 0, 15,
                    rr_ui_title_screen_loadout_button_init(0),
                    rr_ui_title_screen_loadout_button_init(1),
                    rr_ui_title_screen_loadout_button_init(2),
                    rr_ui_title_screen_loadout_button_init(3),
                    rr_ui_title_screen_loadout_button_init(4),
                    rr_ui_title_screen_loadout_button_init(5),
                    rr_ui_title_screen_loadout_button_init(6),
                    rr_ui_title_screen_loadout_button_init(7),
                    rr_ui_title_screen_loadout_button_init(8),
                    rr_ui_title_screen_loadout_button_init(9),
                    rr_ui_title_screen_loadout_button_init(10),
                    rr_ui_title_screen_loadout_button_init(11), NULL),
                rr_ui_h_container_init(
                    rr_ui_container_init(), 0, 15,
                    rr_ui_title_screen_loadout_button_init(12),
                    rr_ui_title_screen_loadout_button_init(13),
                    rr_ui_title_screen_loadout_button_init(14),
                    rr_ui_title_screen_loadout_button_init(15),
                    rr_ui_title_screen_loadout_button_init(16),
                    rr_ui_title_screen_loadout_button_init(17),
                    rr_ui_title_screen_loadout_button_init(18),
                    rr_ui_title_screen_loadout_button_init(19),
                    rr_ui_title_screen_loadout_button_init(20),
                    rr_ui_title_screen_loadout_button_init(21),
                    rr_ui_title_screen_loadout_button_init(22),
                    rr_ui_title_screen_loadout_button_init(23), NULL),
                respawn, NULL),
            0, 1),
        loadout_should_show);
}
