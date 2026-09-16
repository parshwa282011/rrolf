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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Utilities.h>
#include <Shared/pb.h>

static uint8_t dev_squad_panel_container_should_show(struct rr_ui_element *this,
                                                     struct rr_game *game)
{
    return game->is_dev && game->menu_open == rr_game_menu_dev_squad_panel;
}

static uint8_t dev_squad_panel_button_should_show(struct rr_ui_element *this,
                                                  struct rr_game *game)
{
    return game->is_dev;
}

static void dev_squad_panel_container_animate(struct rr_ui_element *this,
                                              struct rr_game *game)
{
    this->width = this->abs_width;
    this->height = this->abs_height;
    rr_renderer_translate(
        game->renderer, -(this->x + this->abs_width / 2) * 2 * this->animation,
        0);
}

static void dev_squad_panel_toggle_button_on_render(struct rr_ui_element *this,
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
    rr_renderer_scale(renderer, 1.2);
    rr_renderer_set_fill(renderer, 0xffffffff);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 12.00, 1.62);
    rr_renderer_line_to(renderer, 12.00, -1.62);
    rr_renderer_bezier_curve_to(renderer, 10.35, -2.20, 9.31, -2.37, 8.78,
                                -3.64);
    rr_renderer_bezier_curve_to(renderer, 8.25, -4.91, 8.88, -5.77, 9.63,
                                -7.34);
    rr_renderer_line_to(renderer, 7.34, -9.63);
    rr_renderer_bezier_curve_to(renderer, 5.78, -8.89, 4.91, -8.25, 3.64,
                                -8.78);
    rr_renderer_bezier_curve_to(renderer, 2.37, -9.31, 2.20, -10.36, 1.62,
                                -12.00);
    rr_renderer_line_to(renderer, -1.62, -12.00);
    rr_renderer_bezier_curve_to(renderer, -2.20, -10.37, -2.37, -9.31, -3.64,
                                -8.78);
    rr_renderer_bezier_curve_to(renderer, -4.91, -8.25, -5.77, -8.88, -7.34,
                                -9.63);
    rr_renderer_line_to(renderer, -9.63, -7.34);
    rr_renderer_bezier_curve_to(renderer, -8.88, -5.78, -8.25, -4.91, -8.78,
                                -3.64);
    rr_renderer_bezier_curve_to(renderer, -9.31, -2.37, -10.37, -2.20, -12.00,
                                -1.62);
    rr_renderer_line_to(renderer, -12.00, 1.62);
    rr_renderer_bezier_curve_to(renderer, -10.37, 2.20, -9.31, 2.37, -8.78,
                                3.64);
    rr_renderer_bezier_curve_to(renderer, -8.25, 4.92, -8.90, 5.80, -9.63,
                                7.34);
    rr_renderer_line_to(renderer, -7.34, 9.63);
    rr_renderer_bezier_curve_to(renderer, -5.78, 8.88, -4.91, 8.25, -3.64,
                                8.78);
    rr_renderer_bezier_curve_to(renderer, -2.37, 9.31, -2.20, 10.36, -1.62,
                                12.00);
    rr_renderer_line_to(renderer, 1.62, 12.00);
    rr_renderer_bezier_curve_to(renderer, 2.20, 10.36, 2.37, 9.31, 3.64, 8.78);
    rr_renderer_bezier_curve_to(renderer, 4.91, 8.25, 5.76, 8.88, 7.34, 9.63);
    rr_renderer_line_to(renderer, 9.63, 7.34);
    rr_renderer_bezier_curve_to(renderer, 8.88, 5.78, 8.25, 4.91, 8.78, 3.64);
    rr_renderer_bezier_curve_to(renderer, 9.31, 2.37, 10.37, 2.20, 12.00, 1.62);
    rr_renderer_arc(renderer, 0, 0, 4);
    rr_renderer_fill(renderer);
}

static void dev_squad_panel_toggle_button_on_event(struct rr_ui_element *this,
                                                   struct rr_game *game)
{
    if (game->input_data->mouse_buttons_up_this_tick & 1)
    {
        if (game->pressed != this)
            return;
        if (game->menu_open == rr_game_menu_dev_squad_panel)
            game->menu_open = rr_game_menu_none;
        else
            game->menu_open = rr_game_menu_dev_squad_panel;
    }
}

struct rr_ui_element *rr_ui_dev_panel_toggle_button_init()
{
    struct rr_ui_element *this = rr_ui_element_init();
    rr_ui_set_background(this, 0x80888888);
    this->abs_width = this->abs_height = this->width = this->height = 40;
    this->should_show = dev_squad_panel_button_should_show;
    this->on_event = dev_squad_panel_toggle_button_on_event;
    this->on_render = dev_squad_panel_toggle_button_on_render;
    return this;
}

// ---- mob summoning ----

static void summon_mob_id_dec(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_mob_id =
        (game->developer_cheats.summon_mob_id + rr_mob_id_max - 1) %
        rr_mob_id_max;
}

static void summon_mob_id_inc(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_mob_id =
        (game->developer_cheats.summon_mob_id + 1) % rr_mob_id_max;
}

static void summon_mob_id_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Mob: %s",
            RR_MOB_NAMES[game->developer_cheats.summon_mob_id]);
}

static void summon_rarity_dec(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_rarity =
        (game->developer_cheats.summon_rarity + rr_rarity_id_max - 1) %
        rr_rarity_id_max;
}

static void summon_rarity_inc(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_rarity =
        (game->developer_cheats.summon_rarity + 1) % rr_rarity_id_max;
}

static void summon_rarity_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Rarity: %s",
            RR_RARITY_NAMES[game->developer_cheats.summon_rarity]);
}

static void summon_mob(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    struct proto_bug encoder;
    proto_bug_init(&encoder, RR_OUTGOING_PACKET);
    proto_bug_write_uint8(&encoder, rr_serverbound_dev_summon, "header");
    proto_bug_write_uint8(&encoder, game->developer_cheats.summon_mob_id, "id");
    proto_bug_write_uint8(&encoder, game->developer_cheats.summon_rarity,
                          "rarity");

    rr_websocket_send(&game->socket, encoder.current - encoder.start);
}

static struct rr_ui_element *summon_mob_row_init(struct rr_game *game)
{
    game->developer_cheats.summon_rarity = rr_rarity_id_ultimate;

    struct rr_ui_element *id_dec = rr_ui_labeled_button_init("-", 20, 0);
    id_dec->on_event = summon_mob_id_dec;
    struct rr_ui_element *id_inc = rr_ui_labeled_button_init("+", 20, 0);
    id_inc->on_event = summon_mob_id_inc;
    struct rr_ui_element *rarity_dec = rr_ui_labeled_button_init("-", 20, 0);
    rarity_dec->on_event = summon_rarity_dec;
    struct rr_ui_element *rarity_inc = rr_ui_labeled_button_init("+", 20, 0);
    rarity_inc->on_event = summon_rarity_inc;
    struct rr_ui_element *summon_button =
        rr_ui_labeled_button_init("Summon", 20, 0);
    summon_button->fill = 0x80ffffff;
    summon_button->on_event = summon_mob;

    return rr_ui_set_justify(
        rr_ui_h_container_init(
            rr_ui_container_init(), 0, 6, id_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, summon_mob_id_text),
            id_inc, rarity_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, summon_rarity_text),
            rarity_inc, summon_button, NULL),
        -1, -1);
}

// ---- portal summoning ----

static void summon_portal_rarity_dec(struct rr_ui_element *this,
                                     struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_portal_rarity =
        (game->developer_cheats.summon_portal_rarity + rr_rarity_id_max - 1) %
        rr_rarity_id_max;
}

static void summon_portal_rarity_inc(struct rr_ui_element *this,
                                     struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.summon_portal_rarity =
        (game->developer_cheats.summon_portal_rarity + 1) % rr_rarity_id_max;
}

static void summon_portal_rarity_text(struct rr_ui_element *this,
                                      struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Rarity: %s",
            RR_RARITY_NAMES[game->developer_cheats.summon_portal_rarity]);
}

static void summon_portal(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    struct proto_bug encoder;
    proto_bug_init(&encoder, RR_OUTGOING_PACKET);
    proto_bug_write_uint8(&encoder, rr_serverbound_dev_summon_portal,
                          "header");
    proto_bug_write_uint8(&encoder, game->developer_cheats.summon_portal_rarity,
                          "rarity");
    proto_bug_write_string(&encoder, game->developer_cheats.summon_portal_dimension,
                           24, "target dimension");
    proto_bug_write_string(&encoder, game->developer_cheats.summon_portal_url,
                           64, "target server url");

    rr_websocket_send(&game->socket, encoder.current - encoder.start);
}

static struct rr_ui_element *summon_portal_row_init(struct rr_game *game)
{
    game->developer_cheats.summon_portal_rarity = rr_rarity_id_common;
    strcpy(game->developer_cheats.summon_portal_dimension, "Hell Creek Med");
    strcpy(game->developer_cheats.summon_portal_url, "ws://127.0.0.1:6768");

    struct rr_ui_element *dimension_input = rr_ui_text_input_init(
        140, 24, &game->developer_cheats.summon_portal_dimension[0], 24,
        "_0x4348");
    struct rr_ui_element *url_input = rr_ui_text_input_init(
        170, 24, &game->developer_cheats.summon_portal_url[0], 64, "_0x4349");
    struct rr_ui_element *rarity_dec = rr_ui_labeled_button_init("-", 20, 0);
    rarity_dec->on_event = summon_portal_rarity_dec;
    struct rr_ui_element *rarity_inc = rr_ui_labeled_button_init("+", 20, 0);
    rarity_inc->on_event = summon_portal_rarity_inc;
    struct rr_ui_element *summon_button =
        rr_ui_labeled_button_init("Summon", 20, 0);
    summon_button->fill = 0x80ffffff;
    summon_button->on_event = summon_portal;

    return rr_ui_set_justify(
        rr_ui_h_container_init(
            rr_ui_container_init(), 0, 6,
            rr_ui_text_init("Portal:", 16, 0xffffffff), dimension_input,
            url_input, rarity_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, summon_portal_rarity_text),
            rarity_inc, summon_button, NULL),
        -1, -1);
}

// ---- petal granting ----

static void give_petal_id_dec(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    uint8_t id = game->developer_cheats.give_petal_id;
    game->developer_cheats.give_petal_id = id <= 1 ? rr_petal_id_max - 1 : id - 1;
}

static void give_petal_id_inc(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    uint8_t id = game->developer_cheats.give_petal_id;
    game->developer_cheats.give_petal_id = id >= rr_petal_id_max - 1 ? 1 : id + 1;
}

static void give_petal_id_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Petal: %s",
            RR_PETAL_NAMES[game->developer_cheats.give_petal_id]);
}

static void give_petal_rarity_dec(struct rr_ui_element *this,
                                  struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.give_petal_rarity =
        (game->developer_cheats.give_petal_rarity + rr_rarity_id_max - 1) %
        rr_rarity_id_max;
}

static void give_petal_rarity_inc(struct rr_ui_element *this,
                                  struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.give_petal_rarity =
        (game->developer_cheats.give_petal_rarity + 1) % rr_rarity_id_max;
}

static void give_petal_rarity_text(struct rr_ui_element *this,
                                   struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Rarity: %s",
            RR_RARITY_NAMES[game->developer_cheats.give_petal_rarity]);
}

static void give_petal_count_dec(struct rr_ui_element *this,
                                 struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    uint32_t count = game->developer_cheats.give_petal_count;
    game->developer_cheats.give_petal_count = count > 10 ? count - 10 : 1;
}

static void give_petal_count_inc(struct rr_ui_element *this,
                                 struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    game->developer_cheats.give_petal_count += 10;
}

static void give_petal_count_text(struct rr_ui_element *this,
                                  struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Count: %u",
            game->developer_cheats.give_petal_count);
}

static void give_petal(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    struct proto_bug encoder;
    proto_bug_init(&encoder, RR_OUTGOING_PACKET);
    proto_bug_write_uint8(&encoder, rr_serverbound_dev_give_petal, "header");
    proto_bug_write_uint8(&encoder, game->developer_cheats.give_petal_id, "id");
    proto_bug_write_uint8(&encoder, game->developer_cheats.give_petal_rarity,
                          "rarity");
    proto_bug_write_varuint(&encoder, game->developer_cheats.give_petal_count,
                            "count");

    rr_websocket_send(&game->socket, encoder.current - encoder.start);
}

static struct rr_ui_element *give_petal_row_init(struct rr_game *game)
{
    game->developer_cheats.give_petal_id = rr_petal_id_basic;
    game->developer_cheats.give_petal_rarity = rr_rarity_id_ultimate;
    game->developer_cheats.give_petal_count = 10;

    struct rr_ui_element *id_dec = rr_ui_labeled_button_init("-", 20, 0);
    id_dec->on_event = give_petal_id_dec;
    struct rr_ui_element *id_inc = rr_ui_labeled_button_init("+", 20, 0);
    id_inc->on_event = give_petal_id_inc;
    struct rr_ui_element *rarity_dec = rr_ui_labeled_button_init("-", 20, 0);
    rarity_dec->on_event = give_petal_rarity_dec;
    struct rr_ui_element *rarity_inc = rr_ui_labeled_button_init("+", 20, 0);
    rarity_inc->on_event = give_petal_rarity_inc;
    struct rr_ui_element *count_dec = rr_ui_labeled_button_init("-", 20, 0);
    count_dec->on_event = give_petal_count_dec;
    struct rr_ui_element *count_inc = rr_ui_labeled_button_init("+", 20, 0);
    count_inc->on_event = give_petal_count_inc;
    struct rr_ui_element *give_button =
        rr_ui_labeled_button_init("Give Petal", 20, 0);
    give_button->fill = 0x80ffffff;
    give_button->on_event = give_petal;

    return rr_ui_set_justify(
        rr_ui_h_container_init(
            rr_ui_container_init(), 0, 6, id_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, give_petal_id_text),
            id_inc, rarity_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, give_petal_rarity_text),
            rarity_inc, count_dec,
            rr_ui_dynamic_text_init(16, 0xffffffff, give_petal_count_text),
            count_inc, give_button, NULL),
        -1, -1);
}

// ---- slot count ----

static void slot_count_dec(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    if (game->developer_cheats.slot_count > 1)
        --game->developer_cheats.slot_count;
}

static void slot_count_inc(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    if (game->developer_cheats.slot_count < 10)
        ++game->developer_cheats.slot_count;
}

static void slot_count_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    snprintf(data->text, 32, "Slots: %u", game->developer_cheats.slot_count);
}

static void apply_slot_count(struct rr_ui_element *this, struct rr_game *game)
{
    if (!(game->input_data->mouse_buttons_up_this_tick & 1))
        return;
    struct proto_bug encoder;
    proto_bug_init(&encoder, RR_OUTGOING_PACKET);
    proto_bug_write_uint8(&encoder, rr_serverbound_dev_set_slot_count,
                          "header");
    proto_bug_write_uint8(&encoder, game->developer_cheats.slot_count,
                          "count");

    rr_websocket_send(&game->socket, encoder.current - encoder.start);
}

static struct rr_ui_element *slot_count_row_init(struct rr_game *game)
{
    game->developer_cheats.slot_count = 10;

    struct rr_ui_element *dec = rr_ui_labeled_button_init("-", 20, 0);
    dec->on_event = slot_count_dec;
    struct rr_ui_element *inc = rr_ui_labeled_button_init("+", 20, 0);
    inc->on_event = slot_count_inc;
    struct rr_ui_element *apply_button =
        rr_ui_labeled_button_init("Set Slots", 20, 0);
    apply_button->fill = 0x80ffffff;
    apply_button->on_event = apply_slot_count;

    return rr_ui_set_justify(
        rr_ui_h_container_init(rr_ui_container_init(), 0, 6, dec,
                               rr_ui_dynamic_text_init(16, 0xffffffff,
                                                       slot_count_text),
                               inc, apply_button, NULL),
        -1, -1);
}

// ---- misc sliders ----

static struct rr_ui_element *speed_slider_init(struct rr_game *game)
{
    struct rr_ui_element *element =
        rr_ui_h_slider_init(100, 20, &game->developer_cheats.speed_percent, 1);
    game->developer_cheats.speed_percent = 0.05;

    return element;
}

static struct rr_ui_element *rotation_slider_init(struct rr_game *game)
{
    struct rr_ui_element *element = rr_ui_h_slider_init(
        100, 20, &game->developer_cheats.rotation_percent, 1);
    game->developer_cheats.rotation_percent = 0.05;

    return element;
}

struct rr_ui_element *rr_ui_dev_panel_container_init(struct rr_game *game)
{
    struct rr_ui_element *inner = rr_ui_v_container_init(
        rr_ui_container_init(), 10, 10, summon_mob_row_init(game),
        summon_portal_row_init(game),
        give_petal_row_init(game), slot_count_row_init(game),
        rr_ui_set_justify(
            rr_ui_h_container_init(rr_ui_container_init(), 0, 10,
                                   rr_ui_text_init("Speed:", 20, 0xffffffff),
                                   speed_slider_init(game), NULL),
            -1, -1),
        rr_ui_set_justify(
            rr_ui_h_container_init(rr_ui_container_init(), 0, 10,
                                   rr_ui_text_init("Rotation:", 20,
                                                   0xffffffff),
                                   rotation_slider_init(game), NULL),
            -1, -1),
        NULL);
    for (uint32_t i = 0; i < RR_SQUAD_COUNT; ++i)
        rr_ui_container_add_element(
            inner, rr_ui_squad_container_init(&game->other_squads[i]));
    struct rr_ui_element *this =
        // clang-format off
    rr_ui_pad(
        rr_ui_set_background(
            rr_ui_v_pad(
                rr_ui_set_justify(
                    rr_ui_scroll_container_init(
                        rr_ui_set_background(
                            inner
                        , 0x40ffffff)
                    , 400),
                    -1, -1),
                50),
            0x40ffffff),
        10);
    // clang-format on
    this->animate = dev_squad_panel_container_animate;
    this->should_show = dev_squad_panel_container_should_show;
    return this;
}
