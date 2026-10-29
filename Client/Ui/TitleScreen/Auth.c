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

// Sign up / log in / reset password screens. They sit on top of the title
// screen until the master server has accepted a session (Client/Auth.c).

#include <Client/Ui/Ui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/Auth.h>
#include <Client/DOM.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#define DISCORD_BLUE 0xff5865f2
#define GREEN 0xff3aa84a
#define GREY 0xff666666

#define UNAVAILABLE_NOTE_1 "Google, Apple and email are unavailable right now:"
#define UNAVAILABLE_NOTE_2 "the email service is not working."

static uint8_t overlay_visible(struct rr_ui_element *this, struct rr_game *game)
{
    return game->auth.screen != rr_auth_screen_none;
}

// screens are numbered from 1 in the enum; the chooser counts from 0
static uint8_t choose_screen(struct rr_ui_element *this, struct rr_game *game)
{
    return game->auth.screen ? game->auth.screen - 1 : 0;
}

static int clicked(struct rr_game *game)
{
    return game->input_data->mouse_buttons_up_this_tick & 1;
}

// ---------------------------------------------------------------- messages
// Dynamic text only knows one line, so long messages are wrapped by hand into
// two lines at the nearest space.

#define LINE_CHARS 46

static void split_line(char const *text, int line, char *out, size_t size)
{
    size_t len = strlen(text);
    size_t cut = len;
    if (len > LINE_CHARS)
    {
        cut = LINE_CHARS;
        while (cut > 20 && text[cut] != ' ')
            --cut;
    }
    if (line == 0)
    {
        snprintf(out, size, "%.*s", (int)cut, text);
    }
    else if (cut >= len)
        out[0] = 0;
    else
    {
        char const *rest = text + cut + (text[cut] == ' ');
        snprintf(out, size, "%.*s", LINE_CHARS, rest);
    }
}

#define MESSAGE_LINE(NAME, SOURCE, LINE)                                       \
    static char NAME##_buffer[LINE_CHARS + 8];                                 \
    static void NAME(struct rr_ui_element *this, struct rr_game *game)         \
    {                                                                          \
        struct rr_ui_dynamic_text_metadata *data = this->data;                 \
        split_line(game->SOURCE, LINE, NAME##_buffer, sizeof NAME##_buffer);   \
        data->text = NAME##_buffer;                                            \
    }

MESSAGE_LINE(message_line_1, auth.message, 0)
MESSAGE_LINE(message_line_2, auth.message, 1)
MESSAGE_LINE(notice_line_1, auth.notice, 0)
MESSAGE_LINE(notice_line_2, auth.notice, 1)

static struct rr_ui_element *message_lines()
{
    return rr_ui_v_container_init(
        rr_ui_container_init(), 0, 2,
        rr_ui_dynamic_text_init(14, 0xffffe066, message_line_1),
        rr_ui_dynamic_text_init(14, 0xffffe066, message_line_2), NULL);
}

static struct rr_ui_element *unavailable_note()
{
    return rr_ui_v_container_init(
        rr_ui_container_init(), 0, 2,
        rr_ui_text_init(UNAVAILABLE_NOTE_1, 12, 0xffcccccc),
        rr_ui_text_init(UNAVAILABLE_NOTE_2, 12, 0xffcccccc), NULL);
}

// --------------------------------------------------------------- widgets

static struct rr_ui_element *field(char *label, char *buffer, uint8_t max,
                                   char *dom_name, int password)
{
    struct rr_ui_element *input =
        rr_ui_text_input_init(320, 28, buffer, max, dom_name);
    if (password)
        rr_dom_set_password(dom_name);
    return rr_ui_v_container_init(
        rr_ui_container_init(), 0, 3,
        rr_ui_set_justify(rr_ui_text_init(label, 14, 0xffffffff), -1, 0), input,
        NULL);
}

static struct rr_ui_element *button(char *text, uint32_t fill,
                                    void (*on_event)(struct rr_ui_element *,
                                                     struct rr_game *))
{
    struct rr_ui_element *this = rr_ui_labeled_button_init(text, 26, NULL);
    this->fill = fill;
    this->on_event = on_event;
    return this;
}

static struct rr_ui_element *panel(struct rr_ui_element *content)
{
    return rr_ui_set_background(
        rr_ui_pad(rr_ui_set_justify(content, 0, -1), 18), 0xe0202020);
}

// --------------------------------------------------------------- handlers

static void on_login(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_login(game);
}

static void on_signup_screen(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_show(game, rr_auth_screen_signup);
}

static void on_login_screen(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_show(game, rr_auth_screen_login);
}

static void on_create_account(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_signup(game);
}

static void on_forgot(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_show(game, rr_auth_screen_reset_identify);
}

static void on_discord(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_discord(game);
}

static void on_unavailable(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        snprintf(game->auth.message, sizeof game->auth.message, "%s",
                 "Unavailable: the email service is not working right now");
}

static void on_reset_start(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_reset_start(game);
}

static void on_reset_send(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_reset_send(game);
}

static void on_reset_confirm(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_reset_confirm(game);
}

static void on_profile(struct rr_ui_element *this, struct rr_game *game)
{
    if (clicked(game))
        rr_auth_complete_profile(game);
}

static void social_row_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    static char buffer[48];
    snprintf(buffer, sizeof buffer, "%s",
             game->auth.busy ? "Please wait..." : "or continue with");
    data->text = buffer;
}

static void code_status_text(struct rr_ui_element *this, struct rr_game *game)
{
    struct rr_ui_dynamic_text_metadata *data = this->data;
    static char buffer[64];
    snprintf(buffer, sizeof buffer, "%s",
             game->auth.reset_code_sent ? "Code sent on Discord"
             : game->auth.reset_discord_available
                 ? "Send the code to your Discord"
                 : "Discord is not linked to this account");
    data->text = buffer;
}

// A Discord button that works, and the three that don't yet.
static struct rr_ui_element *social_row()
{
    return rr_ui_h_container_init(
        rr_ui_container_init(), 0, 8,
        button("Discord", DISCORD_BLUE, on_discord),
        button("Google", GREY, on_unavailable),
        button("Apple", GREY, on_unavailable),
        button("Email", GREY, on_unavailable), NULL);
}

// ---------------------------------------------------------------- screens

static struct rr_ui_element *checking_panel()
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Signing you in...", 22, 0xffffffff), message_lines(),
        NULL));
}

static struct rr_ui_element *login_panel(struct rr_game *game)
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Log in", 24, 0xffffffff),
        field("Username or email", game->auth.identifier, 254,
              "auth_identifier", 0),
        field("Password", game->auth.password, 128, "auth_password", 1),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Log in", GREEN, on_login),
                               button("Sign up", 0xff3377cc, on_signup_screen),
                               button("Forgot password", GREY, on_forgot),
                               NULL),
        rr_ui_dynamic_text_init(13, 0xffcccccc, social_row_text),
        social_row(), unavailable_note(), message_lines(), NULL));
}

static struct rr_ui_element *signup_panel(struct rr_game *game)
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Create an account", 24, 0xffffffff),
        field("Email", game->auth.email, 254, "auth_email", 0),
        field("Username (3-64 characters)", game->auth.username, 64,
              "auth_signup_username", 0),
        field("Password (8 or more characters)", game->auth.password, 128,
              "auth_signup_password", 1),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Create account", GREEN, on_create_account),
                               button("Back", GREY, on_login_screen), NULL),
        rr_ui_dynamic_text_init(13, 0xffcccccc, social_row_text), social_row(),
        unavailable_note(), message_lines(), NULL));
}

static struct rr_ui_element *reset_identify_panel(struct rr_game *game)
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Reset your password", 24, 0xffffffff),
        field("Username or email", game->auth.identifier, 254,
              "auth_reset_identifier", 0),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Continue", GREEN, on_reset_start),
                               button("Back", GREY, on_login_screen), NULL),
        message_lines(), NULL));
}

static struct rr_ui_element *reset_code_panel(struct rr_game *game)
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Reset your password", 24, 0xffffffff),
        rr_ui_text_init("Where should we send your code?", 15, 0xffffffff),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Discord", DISCORD_BLUE, on_reset_send),
                               button("Email", GREY, on_unavailable),
                               button("Google", GREY, on_unavailable),
                               button("Apple", GREY, on_unavailable), NULL),
        rr_ui_dynamic_text_init(13, 0xffcccccc, code_status_text),
        unavailable_note(),
        field("Code", game->auth.otp, 12, "auth_otp", 0),
        field("New password", game->auth.new_password, 128, "auth_new_password",
              1),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Reset password", GREEN, on_reset_confirm),
                               button("Back", GREY, on_login_screen), NULL),
        message_lines(), NULL));
}

static struct rr_ui_element *profile_panel(struct rr_game *game)
{
    return panel(rr_ui_v_container_init(
        rr_ui_container_init(), 10, 8,
        rr_ui_text_init("Finish your account", 24, 0xffffffff),
        rr_ui_text_init("Pick the username and password you will use here.", 13,
                        0xffcccccc),
        field("Username (3-64 characters)", game->auth.username, 64,
              "auth_username", 0),
        field("Password (8 or more characters)", game->auth.password, 128,
              "auth_profile_password", 1),
        rr_ui_h_container_init(rr_ui_container_init(), 0, 8,
                               button("Finish", GREEN, on_profile),
                               button("Cancel", GREY, on_login_screen), NULL),
        message_lines(), NULL));
}

struct rr_ui_element *rr_ui_auth_container_init(struct rr_game *game)
{
    // order matches enum rr_auth_screen (minus rr_auth_screen_none)
    struct rr_ui_element *this = rr_ui_multi_choose_element_init(
        choose_screen, checking_panel(), login_panel(game), signup_panel(game),
        reset_identify_panel(game), reset_code_panel(game),
        profile_panel(game), NULL);
    this = rr_ui_set_justify(this, 0, 0);
    this->should_show = overlay_visible;
    this->animate = rr_ui_instant_hide_animate;
    return this;
}
