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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/Ctf.h>
#include <Client/DOM.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Ctf.h>

// The host's panel for capture the flag. The host makes a private squad, the
// other teams tell the host their squad codes, the host types them in, links
// the squads and starts the match. The server does the checking, what it says
// shows at the bottom.

#define CODE_INPUT_0 "_0x434c"
#define CODE_INPUT_1 "_0x434d"
#define CODE_INPUT_2 "_0x434e"

static char message_line[128];
static char team_lines[RR_CTF_MAX_TEAMS][64];
static char hint_line[128];

// only the leader of a private squad can host
static uint8_t can_host(struct rr_game *game)
{
    return game->joined_squad && game->socket_ready &&
           game->squad.squad_private &&
           game->squad.squad_pos == game->squad.squad_owner;
}

static uint8_t panel_should_show(struct rr_ui_element *this,
                                 struct rr_game *game)
{
    return game->menu_open == rr_game_menu_ctf_host &&
           rr_ctf_client_selected(game) && !game->simulation_ready;
}

static void panel_animate(struct rr_ui_element *this, struct rr_game *game)
{
    this->width = this->abs_width;
    this->height = this->abs_height;
    rr_renderer_translate(
        game->renderer, -(this->x + this->abs_width / 2) * 2 * this->animation,
        0);
    struct rr_game_ctf *c = &game->ctf;
    if (!game->joined_squad)
        snprintf(hint_line, sizeof hint_line,
                 "First press Create Squad, you are the leader of a private squad.");
    else if (!game->squad.squad_private)
        snprintf(hint_line, sizeof hint_line,
                 "Only private squads can play. Make a new one with Create Squad.");
    else if (game->squad.squad_pos != game->squad.squad_owner)
        snprintf(hint_line, sizeof hint_line,
                 "Only your squad's leader hosts.");
    else if (c->team_count == 0)
        snprintf(hint_line, sizeof hint_line,
                 "Get the other squads' codes and type them in, then link.");
    else
        snprintf(hint_line, sizeof hint_line, "%s",
                 c->phase == rr_ctf_phase_lobby
                     ? "Squads are linked. Start when everyone is on."
                     : "A match is running.");
    char text[96];
    if (rr_ctf_client_message_text(game, text, sizeof text) != NULL)
        snprintf(message_line, sizeof message_line, "%s", text);
    else
        message_line[0] = 0;
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        if (c->has_body && t < c->team_count)
            snprintf(team_lines[t], sizeof team_lines[t], "%s team: %u player%s",
                     rr_ctf_team_name(t), c->teams[t].members,
                     c->teams[t].members == 1 ? "" : "s");
        else
            team_lines[t][0] = 0;
    }
}

static void link_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    uint8_t any = 0;
    for (uint8_t i = 0; i < RR_CTF_MAX_TEAMS - 1; ++i)
        any |= game->ctf.codes[i][0] != 0;
    data->clickable = can_host(game) && any &&
                      (!game->ctf.active ||
                       game->ctf.phase == rr_ctf_phase_lobby);
    if (!data->clickable)
        return;
    if (game->input_data->mouse_buttons_up_this_tick & 1 &&
        game->pressed == this)
        rr_ctf_client_send_create(game);
    game->cursor = rr_game_cursor_pointer;
}

// starting takes two clicks, so that it does not happen by accident
static void start_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    struct rr_game_ctf *c = &game->ctf;
    data->clickable = game->socket_ready && c->active && c->can_start;
    data->text = c->start_confirm > 0 ? "Click again to start" : "Start";
    if (!data->clickable)
        return;
    if (game->input_data->mouse_buttons_up_this_tick & 1 &&
        game->pressed == this)
    {
        if (c->start_confirm > 0)
        {
            c->start_confirm = 0;
            rr_ctf_client_send_start(game);
        }
        else
            c->start_confirm = 5;
    }
    game->cursor = rr_game_cursor_pointer;
}

static void cancel_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    struct rr_game_ctf *c = &game->ctf;
    data->clickable = can_host(game) && c->active && c->team_count > 0;
    if (!data->clickable)
        return;
    if (game->input_data->mouse_buttons_up_this_tick & 1 &&
        game->pressed == this)
        rr_ctf_client_send_cancel(game);
    game->cursor = rr_game_cursor_pointer;
}

static void button_animate(struct rr_ui_element *this, struct rr_game *game)
{
    rr_ui_default_animate(this, game);
    struct rr_ui_labeled_button_metadata *data = this->data;
    rr_ui_set_background(this, data->clickable ? 0x80ffffff : 0x80999999);
}

static struct rr_ui_element *button_init(
    char *label, void (*on_event)(struct rr_ui_element *, struct rr_game *))
{
    struct rr_ui_element *this = rr_ui_labeled_button_init(label, 26, NULL);
    this->on_event = on_event;
    this->animate = button_animate;
    return this;
}

// clang-format off
static struct rr_ui_element *code_row_init(struct rr_game *game, uint8_t index,
                                           char *name)
{
    char label[16];
    snprintf(label, sizeof label, "Squad %u:", index + 2);
    // the text has to outlive the element
    char *label_copy = strdup(label);
    return rr_ui_h_container_init(rr_ui_container_init(), 0, 10,
        rr_ui_text_init(label_copy, 16, 0xffffffff),
        rr_ui_set_fill_stroke(
            rr_ui_h_container_init(rr_ui_container_init(), 0, 0,
                rr_ui_text_input_init(150, 18, game->ctf.codes[index], 15, name),
                NULL),
        0x00000000, 0x00000000),
        NULL);
}

struct rr_ui_element *rr_ui_ctf_host_container_init(struct rr_game *game)
{
    struct rr_ui_element *this = rr_ui_pad(
        rr_ui_set_background(
            rr_ui_v_pad(
                rr_ui_set_justify(
                    rr_ui_v_container_init(
                        rr_ui_container_init(), 10, 10,
                        rr_ui_text_init("Capture the Flag", 24, 0xffffffff),
                        rr_ui_text_init(hint_line, 14, 0xffbbbbbb),
                        rr_ui_text_init("Codes of the other squads (2 to 4 teams):", 15, 0xffffffff),
                        code_row_init(game, 0, CODE_INPUT_0),
                        code_row_init(game, 1, CODE_INPUT_1),
                        code_row_init(game, 2, CODE_INPUT_2),
                        rr_ui_h_container_init(rr_ui_container_init(), 0, 10,
                            button_init("Link squads", link_on_event),
                            button_init("Start", start_on_event),
                            button_init("Cancel", cancel_on_event),
                            NULL),
                        rr_ui_text_init(message_line, 15, 0xffffd060),
                        rr_ui_text_init(team_lines[0], 14, 0xffff6a6a),
                        rr_ui_text_init(team_lines[1], 14, 0xff6a9bff),
                        rr_ui_text_init(team_lines[2], 14, 0xff6ae08a),
                        rr_ui_text_init(team_lines[3], 14, 0xffeed46a),
                        NULL),
                    -1, -1),
                50),
            0x40ffffff),
        10);
    // clang-format on
    this->animate = panel_animate;
    this->should_show = panel_should_show;
    return this;
}

// ---- the button that opens it

static uint8_t toggle_should_show(struct rr_ui_element *this,
                                  struct rr_game *game)
{
    return rr_ctf_client_selected(game) && !game->simulation_ready;
}

static void toggle_on_render(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_renderer *renderer = game->renderer;
    if (game->focused == this)
        renderer->state.filter.amount = 0.2;
    rr_renderer_scale(renderer, renderer->scale);
    rr_renderer_set_fill(renderer, this->fill);
    renderer->state.filter.amount += 0.2;
    rr_renderer_begin_path(renderer);
    rr_renderer_round_rect(renderer, -this->abs_width / 2,
                           -this->abs_height / 2, this->abs_width,
                           this->abs_height, 6);
    rr_renderer_fill(renderer);
    // a flag
    rr_renderer_set_line_join(renderer, 1);
    rr_renderer_set_stroke(renderer, 0xffffffff);
    rr_renderer_set_line_width(renderer, 3);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -9, 14);
    rr_renderer_line_to(renderer, -9, -14);
    rr_renderer_stroke(renderer);
    rr_renderer_set_fill(renderer, 0xffe03c3c);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -8, -14);
    rr_renderer_line_to(renderer, 14, -7);
    rr_renderer_line_to(renderer, -8, 1);
    rr_renderer_fill(renderer);
}

static void toggle_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    if (game->input_data->mouse_buttons_up_this_tick & 1)
    {
        if (game->pressed != this)
            return;
        if (game->menu_open == rr_game_menu_ctf_host)
            game->menu_open = rr_game_menu_none;
        else
            game->menu_open = rr_game_menu_ctf_host;
    }
    game->cursor = rr_game_cursor_pointer;
}

struct rr_ui_element *rr_ui_ctf_toggle_button_init()
{
    struct rr_ui_element *this = rr_ui_element_init();
    rr_ui_set_background(this, 0x80888888);
    this->abs_width = this->abs_height = this->width = this->height = 40;
    this->on_event = toggle_on_event;
    this->on_render = toggle_on_render;
    this->should_show = toggle_should_show;
    return this;
}
