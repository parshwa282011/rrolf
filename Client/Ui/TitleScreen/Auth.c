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

// The "Signing you in..." cover over the title screen while the stored login is
// checked (Client/Auth.c). Logging in itself is the page
// Client/public/login.html.

#include <Client/Ui/Ui.h>

#include <stdio.h>
#include <string.h>

#include <Client/Auth.h>
#include <Client/Game.h>
#include <Client/Ui/Engine.h>

static uint8_t overlay_visible(struct rr_ui_element *this, struct rr_game *game)
{
    return game->auth.screen != rr_auth_screen_none;
}

static void message_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    data->text = game->auth.message;
}

struct rr_ui_element *rr_ui_auth_container_init(struct rr_game *game)
{
    struct rr_ui_element *panel = rr_ui_set_background(
        rr_ui_pad(rr_ui_v_container_init(
                      rr_ui_container_init(), 10, 8,
                      rr_ui_text_init("Signing you in...", 22, 0xffffffff),
                      rr_ui_dynamic_text_init(14, 0xffffe066, message_text),
                      NULL),
                  18),
        0xe0202020);
    struct rr_ui_element *this = rr_ui_set_justify(panel, 0, 0);
    this->should_show = overlay_visible;
    this->animate = rr_ui_instant_hide_animate;
    return this;
}
