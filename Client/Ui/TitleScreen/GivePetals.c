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

#ifdef PDEV

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/DOM.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Utilities.h>
#include <Shared/pb.h>

// PDEV builds only. A panel to ask the game server for petals: the PIN, the
// petal, its rarity and the amount are sent in a rr_dev_cheat_give_petals
// packet, and the server (PDEV build) checks the PIN and answers with a
// rr_clientbound_account_reply, whose message shows in the panel.

#define PIN_INPUT "_0x4349"
#define AMOUNT_INPUT "_0x434a"

static char pin_text[16];
static char amount_text[16];
static float petal_slider;
static float rarity_slider;
static char petal_label[48];
static char rarity_label[32];

static uint8_t selected_petal()
{
    uint8_t id = 1 + petal_slider * (rr_petal_id_max - 1);
    return id >= rr_petal_id_max ? rr_petal_id_max - 1 : id;
}

static uint8_t selected_rarity()
{
    uint8_t rarity = rarity_slider * rr_rarity_id_max;
    return rarity >= rr_rarity_id_max ? rr_rarity_id_max - 1 : rarity;
}

static uint8_t give_petals_container_should_show(struct rr_ui_element *this,
                                                 struct rr_game *game)
{
    return game->menu_open == rr_game_menu_give_petals;
}

static void give_petals_container_animate(struct rr_ui_element *this,
                                          struct rr_game *game)
{
    this->width = this->abs_width;
    this->height = this->abs_height;
    rr_renderer_translate(
        game->renderer, -(this->x + this->abs_width / 2) * 2 * this->animation,
        0);
}

static void give_petals_toggle_button_on_render(struct rr_ui_element *this,
                                                struct rr_game *game)
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
    // a plus sign
    rr_renderer_set_fill(renderer, 0xffffffff);
    rr_renderer_begin_path(renderer);
    rr_renderer_fill_rect(renderer, -10, -2.5, 20, 5);
    rr_renderer_fill_rect(renderer, -2.5, -10, 5, 20);
}

static void give_petals_toggle_button_on_event(struct rr_ui_element *this,
                                               struct rr_game *game)
{
    if (game->input_data->mouse_buttons_up_this_tick & 1)
    {
        if (game->pressed != this)
            return;
        if (game->menu_open == rr_game_menu_give_petals)
            game->menu_open = rr_game_menu_none;
        else
            game->menu_open = rr_game_menu_give_petals;
    }
    game->cursor = rr_game_cursor_pointer;
}

struct rr_ui_element *rr_ui_give_petals_toggle_button_init()
{
    struct rr_ui_element *this = rr_ui_element_init();
    rr_ui_set_background(this, 0x80888888);
    this->abs_width = this->abs_height = this->width = this->height = 40;
    this->on_event = give_petals_toggle_button_on_event;
    this->on_render = give_petals_toggle_button_on_render;
    return this;
}

static void give_button_on_event(struct rr_ui_element *this,
                                 struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    if (game->socket_ready)
    {
        if (game->input_data->mouse_buttons_up_this_tick & 1)
        {
            uint64_t amount = strtoull(amount_text, NULL, 10);
            if (pin_text[0] == 0)
                snprintf(game->auth_message, sizeof game->auth_message,
                         "Enter the PIN.");
            else if (amount == 0)
                snprintf(game->auth_message, sizeof game->auth_message,
                         "Enter an amount.");
            else
            {
                snprintf(game->auth_message, sizeof game->auth_message,
                         "Checking...");
                struct proto_bug encoder;
                proto_bug_init(&encoder, RR_OUTGOING_PACKET);
                proto_bug_write_uint8(&encoder, game->socket.quick_verification,
                                      "qv");
                proto_bug_write_uint8(&encoder, rr_serverbound_dev_cheat,
                                      "header");
                proto_bug_write_uint8(&encoder, rr_dev_cheat_give_petals,
                                      "cheat type");
                proto_bug_write_string(&encoder, pin_text, sizeof pin_text,
                                       "pin");
                proto_bug_write_uint8(&encoder, selected_petal(), "id");
                proto_bug_write_uint8(&encoder, selected_rarity(), "rarity");
                proto_bug_write_varuint(&encoder, amount, "count");
                rr_websocket_send(&game->socket,
                                  encoder.current - encoder.start);
            }
        }
        game->cursor = rr_game_cursor_pointer;
        data->clickable = 1;
    }
    else
        data->clickable = 0;
}

static void give_button_animate(struct rr_ui_element *this,
                                struct rr_game *game)
{
    rr_ui_default_animate(this, game);
    if (!game->socket_ready)
        rr_ui_set_background(this, 0x80999999);
    else
        rr_ui_set_background(this, 0x80ffffff);
}

static void petal_slider_animate(struct rr_ui_element *this,
                                 struct rr_game *game)
{
    uint8_t id = selected_petal();
    snprintf(petal_label, sizeof petal_label, "%u (%s)", id, RR_PETAL_NAMES[id]);
}

static void rarity_slider_animate(struct rr_ui_element *this,
                                  struct rr_game *game)
{
    uint8_t rarity = selected_rarity();
    snprintf(rarity_label, sizeof rarity_label, "%u (%s)", rarity,
             RR_RARITY_NAMES[rarity]);
}

// the PIN is hidden while typing
static struct rr_ui_element *pin_input_init()
{
    struct rr_ui_element *element = rr_ui_text_input_init(
        100, 18, &pin_text[0], sizeof pin_text - 1, PIN_INPUT);
    EM_ASM({ document.getElementById(UTF8ToString($0)).type = "password"; },
           PIN_INPUT);
    return element;
}

// clang-format off
static struct rr_ui_element *labeled_input_init(char *label, char *text,
                                                char *name, uint32_t max,
                                                float width, uint8_t pin)
{
    struct rr_ui_element *input = pin ? pin_input_init()
        : rr_ui_text_input_init(width, 18, text, max, name);
    return rr_ui_h_container_init(rr_ui_container_init(), 0, 10,
        rr_ui_text_init(label, 16, 0xffffffff),
        rr_ui_set_fill_stroke(
            rr_ui_h_container_init(rr_ui_container_init(), 0, 0, input, NULL),
        0x00000000, 0x00000000),
        NULL);
}

static struct rr_ui_element *petal_slider_init()
{
    struct rr_ui_element *element = rr_ui_v_container_init(
        rr_ui_container_init(), 0, 0,
        rr_ui_h_container_init(rr_ui_container_init(), 0, 5,
            rr_ui_text_init("Petal:", 16, 0xffffffff),
            rr_ui_h_slider_init(155, 20, &petal_slider, 1),
            NULL),
        rr_ui_text_init(petal_label, 16, 0xffffffff),
        NULL);
    element->animate = petal_slider_animate;
    return element;
}

static struct rr_ui_element *rarity_slider_init()
{
    struct rr_ui_element *element = rr_ui_v_container_init(
        rr_ui_container_init(), 0, 0,
        rr_ui_h_container_init(rr_ui_container_init(), 0, 5,
            rr_ui_text_init("Rarity:", 16, 0xffffffff),
            rr_ui_h_slider_init(155, 20, &rarity_slider, 1),
            NULL),
        rr_ui_text_init(rarity_label, 16, 0xffffffff),
        NULL);
    rarity_slider = (float)rr_rarity_id_ancient / rr_rarity_id_max;
    element->animate = rarity_slider_animate;
    return element;
}

struct rr_ui_element *rr_ui_give_petals_container_init(struct rr_game *game)
{
    struct rr_ui_element *give = rr_ui_labeled_button_init("Give", 30, 0);
    give->on_event = give_button_on_event;
    give->animate = give_button_animate;
    struct rr_ui_element *this = rr_ui_pad(
        rr_ui_set_background(
            rr_ui_v_pad(
                rr_ui_set_justify(
                    rr_ui_v_container_init(
                        rr_ui_container_init(), 10, 10,
                        rr_ui_text_init("Give Petals", 24, 0xffffffff),
                        labeled_input_init("PIN:", pin_text, PIN_INPUT,
                                           sizeof pin_text - 1, 100, 1),
                        petal_slider_init(),
                        rarity_slider_init(),
                        labeled_input_init("Amount:", amount_text,
                                           AMOUNT_INPUT, sizeof amount_text - 1,
                                           100, 0),
                        give,
                        rr_ui_text_init(game->auth_message, 14, 0xffbbbbbb),
                        NULL),
                    -1, -1),
                50),
            0x40ffffff),
        10);
    // clang-format on
    this->animate = give_petals_container_animate;
    this->should_show = give_petals_container_should_show;
    return this;
}

#endif
