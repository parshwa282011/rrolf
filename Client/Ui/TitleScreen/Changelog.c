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

#include <Client/DOM.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Utilities.h>

// What changed, newest first. The button is in the row of links at the top right
// of the title screen and the panel opens like the Terms & Privacy one. Gardn has
// its own list, in the game (gardn/Client/Ui/TitleScreen/Changelog.cc): the
// changes of both games are written in both.

static uint8_t changelog_should_show(struct rr_ui_element *this,
                                     struct rr_game *game)
{
    return game->menu_open == rr_game_menu_changelog;
}

static uint8_t changelog_button_should_show(struct rr_ui_element *this,
                                            struct rr_game *game)
{
    return !game->simulation_ready;
}

static void changelog_animate(struct rr_ui_element *this, struct rr_game *game)
{
    this->width = this->abs_width;
    this->height = this->abs_height;
    rr_renderer_translate(
        game->renderer, -(this->x + this->abs_width / 2) * 2 * this->animation,
        0);
}

static void changelog_button_on_event(struct rr_ui_element *this,
                                      struct rr_game *game)
{
    struct rr_ui_labeled_button_metadata *data = this->data;
    data->clickable = 1;
    if (game->input_data->mouse_buttons_up_this_tick & 1 && game->pressed == this)
        game->menu_open = game->menu_open == rr_game_menu_changelog
                              ? rr_game_menu_none
                              : rr_game_menu_changelog;
    game->cursor = rr_game_cursor_pointer;
}

struct rr_ui_element *rr_ui_changelog_button_init()
{
    struct rr_ui_element *this =
        rr_ui_labeled_button_init("Changelog", 30, NULL);
    struct rr_ui_labeled_button_metadata *data = this->data;
    data->clickable = 1;
    this->on_event = changelog_button_on_event;
    this->should_show = changelog_button_should_show;
    rr_ui_set_background(this, 0xff5a9fdb);
    return this;
}

static void changelog_divider_on_render(struct rr_ui_element *this,
                                        struct rr_game *game)
{
    struct rr_renderer *renderer = game->renderer;
    rr_renderer_scale(renderer, renderer->scale);
    rr_renderer_set_stroke(renderer, this->fill);
    rr_renderer_set_line_width(renderer, this->abs_height / 2);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -this->abs_width / 2, 0);
    rr_renderer_line_to(renderer, this->abs_width / 2, 0);
    rr_renderer_stroke(renderer);
}

static struct rr_ui_element *changelog_divider_init()
{
    struct rr_ui_element *this = rr_ui_element_init();
    this->abs_height = this->height = 6;
    this->abs_width = this->width = 420;
    rr_ui_set_background(this, 0x40000000);
    this->on_render = changelog_divider_on_render;
    return this;
}

#define DATE(text) rr_ui_text_init(text, 20, 0xffffffff)
#define LINE(text) rr_ui_text_init("- " text, 14, 0xffffffff)

// clang-format off
struct rr_ui_element *rr_ui_changelog_container_init(struct rr_game *game)
{
    struct rr_ui_element *list = rr_ui_justify_all(
        rr_ui_v_container_init(rr_ui_container_init(), 0, 6,
            DATE("November 7th 2026"),
            LINE("Changelog panel (this one)"),
            LINE("A link to the game this one is forked from"),
            LINE("Sign up with Apple is off for now"),
            LINE("Gardn: Sandbox mode (two areas, a developer menu), M"),
            LINE("switches between mouse and keyboard movement"),
            changelog_divider_init(),
            DATE("November 6th 2026"),
            LINE("Gardn: who is in the world is backed up every 30 seconds,"),
            LINE("after a crash they get their progress back as saves"),
            changelog_divider_init(),
            DATE("November 4th 2026"),
            LINE("Accounts: the email is confirmed with a code"),
            LINE("Account page: link Discord or Google, add an email,"),
            LINE("delete your account"),
            LINE("Privacy policy and terms of service pages"),
            changelog_divider_init(),
            DATE("November 3rd 2026"),
            LINE("Minigames server: capture the flag, team deathmatch,"),
            LINE("hide and seek"),
            LINE("Leaderboards and a player stats page with search"),
            changelog_divider_init(),
            DATE("November 1st 2026"),
            LINE("Leaderboards of Rysteria and gardn at /leaderboard"),
            changelog_divider_init(),
            DATE("October 31st 2026"),
            LINE("The Gardn button on the title screen"),
            LINE("Chat and the squad list shared between servers"),
            LINE("Drag petals onto loadout slots, N shows the whole map"),
            NULL
        ), -1);
    struct rr_ui_element *this = rr_ui_pad(
        rr_ui_set_background(
            rr_ui_v_pad(
                rr_ui_set_justify(
                    rr_ui_v_container_init(rr_ui_container_init(), 10, 10,
                        rr_ui_text_init("Changelog", 24, 0xffffffff),
                        rr_ui_scroll_container_init(list, 320),
                        NULL
                    ), -1, -1
                ), 50
            ), 0x40ffffff
        ), 10
    );
    this->animate = changelog_animate;
    this->should_show = changelog_should_show;
    return this;
}
// clang-format on
