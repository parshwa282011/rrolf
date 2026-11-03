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

#include <Client/Ui/Engine.h>

#include <Client/Ctf.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Simulation.h>
#include <Client/Socket.h>
#include <Shared/pb.h>

static void continue_to_squad_event(struct rr_ui_element *this,
                                    struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    // team deathmatch: Continue is for when the whole team is dead, or forfeit
    if (game->ctf.mode == rr_mg_mode_tdm && !game->ctf.can_leave)
    {
        data->clickable = 0;
        rr_ui_set_background(this, 0xff777777);
        return;
    }
    rr_ui_set_background(this, 0xff45a8a8);
    if (game->input_data->mouse_buttons_up_this_tick & 1)
    {
        struct proto_bug encoder;
        proto_bug_init(&encoder, RR_OUTGOING_PACKET);
        proto_bug_write_uint8(&encoder, game->socket.quick_verification, "qv");
        proto_bug_write_uint8(&encoder, rr_serverbound_squad_ready, "header");
        rr_websocket_send(&game->socket, encoder.current - encoder.start);
    }
    game->cursor = rr_game_cursor_pointer;
    data->clickable = 1;
}

static uint8_t dead_last_tick = 1;
static uint8_t chosen_text = 1;

static uint8_t game_over(struct rr_ui_element *this, struct rr_game *game)
{
    uint8_t dead = game->flower_dead;
    if (dead && !dead_last_tick)
        chosen_text = rr_frand() >= 0.001;
    dead_last_tick = dead;
    // in capture the flag players respawn by themselves; in team deathmatch and
    // hide and seek they don't
    return !game->cache.hide_ui && game->simulation_ready && dead &&
           (!rr_ctf_client_selected(game) || (game->ctf.active &&
                                              game->ctf.mode != rr_mg_mode_ctf));
}

static uint8_t forfeit_should_show(struct rr_ui_element *this,
                                   struct rr_game *game)
{
    return game->ctf.mode == rr_mg_mode_tdm && !game->ctf.can_leave;
}

static void forfeit_on_event(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    data->clickable = 1;
    if (game->input_data->mouse_buttons_up_this_tick & 1 && game->pressed == this)
        rr_ctf_client_send_forfeit(game);
    game->cursor = rr_game_cursor_pointer;
}

static uint8_t text_choose(struct rr_ui_element *this, struct rr_game *game)
{
    return chosen_text;
}

struct rr_ui_element *rr_ui_finished_game_screen_init()
{
    struct rr_ui_element *leave_game =
        rr_ui_labeled_button_init("Continue", 36, NULL);
    rr_ui_set_background(leave_game, 0xff45a8a8);
    leave_game->on_event = continue_to_squad_event;
    struct rr_ui_element *forfeit = rr_ui_labeled_button_init("Forfeit", 30, NULL);
    rr_ui_set_background(forfeit, 0xffc04040);
    forfeit->on_event = forfeit_on_event;
    forfeit->should_show = forfeit_should_show;
    struct rr_ui_element *this = rr_ui_v_container_init(
        rr_ui_container_init(), 10, 10,
        rr_ui_choose_element_init(
            rr_ui_text_init("You Died", 48, 0xffffffff),
            // easter egg
            rr_ui_text_init("Skill Issue", 48, 0xffffffff),
            text_choose
        ),
        rr_ui_static_space_init(150),
        leave_game,
        forfeit,
        NULL);
    this->should_show = game_over;
    return this;
}