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

#ifdef EMSCRIPTEN
#include <emscripten/wget.h>
#endif

#include <Client/DOM.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Ui/Engine.h>

#include <Shared/Utilities.h>

// The changelog of both games. The list is not in the client: it is the file
// changelog.txt (Client/public, served at /changelog.txt), which gardn reads too, so a
// change that is the same in both games is written once. The client asks for it when it
// starts and shows the entries marked [all] and [rysteria]. The format is described at
// the top of the file. The button is in the row of links at the top right of the title
// screen and the panel opens like the Terms & Privacy one.

#define CL_MAX_LINES 160
#define CL_COLS 56

static char cl_text[CL_MAX_LINES][CL_COLS + 8];
static uint8_t cl_kind[CL_MAX_LINES]; // 0 nothing, 1 a day, 2 a change
static uint32_t cl_count;
static struct rr_ui_element *cl_date_elt[CL_MAX_LINES];
static struct rr_ui_element *cl_item_elt[CL_MAX_LINES];

static void cl_add(uint8_t kind, char const *text)
{
    if (cl_count >= CL_MAX_LINES)
        return;
    cl_kind[cl_count] = kind;
    snprintf(cl_text[cl_count], sizeof cl_text[cl_count], "%s", text);
    ++cl_count;
}

// one change, broken into lines of at most CL_COLS characters at the spaces
static void cl_add_wrapped(char const *text)
{
    char line[CL_COLS + 8];
    char const *at = text;
    uint8_t first = 1;
    while (*at)
    {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        size_t room = CL_COLS - 2;
        size_t len = strlen(at);
        size_t cut = len;
        if (len > room)
        {
            cut = room;
            while (cut > 0 && at[cut] != ' ')
                --cut;
            if (cut == 0)
                cut = room;
        }
        snprintf(line, sizeof line, "%s%.*s", first ? "- " : "  ", (int)cut, at);
        cl_add(2, line);
        first = 0;
        at += cut;
    }
}

static void cl_parse(char const *buf, int size)
{
    cl_count = 0;
    memset(cl_kind, 0, sizeof cl_kind);
    char day[CL_COLS + 8];
    day[0] = 0;
    int pos = 0;
    while (pos < size)
    {
        char line[512];
        int len = 0;
        while (pos < size && buf[pos] != '\n')
        {
            if (buf[pos] != '\r' && len < (int)sizeof line - 1)
                line[len++] = buf[pos];
            ++pos;
        }
        ++pos;
        line[len] = 0;
        if (len == 0 || line[0] == '#')
            continue;
        if (strncmp(line, "== ", 3) == 0)
        {
            snprintf(day, sizeof day, "%.*s", CL_COLS, line + 3);
            continue;
        }
        if (line[0] != '[')
            continue;
        char *close = strchr(line, ']');
        if (close == NULL)
            continue;
        *close = 0;
        if (strcmp(line + 1, "all") != 0 && strcmp(line + 1, "rysteria") != 0)
            continue;
        // the day is written when its first change for this game comes
        if (day[0])
        {
            cl_add(1, day);
            day[0] = 0;
        }
        cl_add_wrapped(close + 1);
    }
    if (cl_count == 0)
        cl_add(2, "Nothing to show yet.");
}

#ifdef EMSCRIPTEN
static void cl_loaded(void *arg, void *buffer, int size)
{
    cl_parse(buffer, size);
}

static void cl_failed(void *arg)
{
    cl_count = 0;
    memset(cl_kind, 0, sizeof cl_kind);
    cl_add(2, "The changelog could not be loaded.");
}
#endif

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

// every line of the table has a "day" element and a "change" element, and shows
// the one that fits what the line is
static uint32_t cl_index(struct rr_ui_element *this, struct rr_ui_element **table)
{
    for (uint32_t i = 0; i < CL_MAX_LINES; ++i)
        if (table[i] == this)
            return i;
    return CL_MAX_LINES;
}

static uint8_t cl_date_should_show(struct rr_ui_element *this,
                                   struct rr_game *game)
{
    uint32_t i = cl_index(this, cl_date_elt);
    return i < cl_count && cl_kind[i] == 1;
}

static uint8_t cl_item_should_show(struct rr_ui_element *this,
                                   struct rr_game *game)
{
    uint32_t i = cl_index(this, cl_item_elt);
    return i < cl_count && cl_kind[i] == 2;
}

// clang-format off
struct rr_ui_element *rr_ui_changelog_container_init(struct rr_game *game)
{
    cl_add(2, "Loading the changelog...");
#ifdef EMSCRIPTEN
    emscripten_async_wget_data("/changelog.txt", NULL, cl_loaded, cl_failed);
#endif
    struct rr_ui_element *list =
        rr_ui_v_container_init(rr_ui_container_init(), 0, 6, NULL);
    for (uint32_t i = 0; i < CL_MAX_LINES; ++i)
    {
        cl_date_elt[i] = rr_ui_text_init(cl_text[i], 20, 0xffffffff);
        cl_date_elt[i]->should_show = cl_date_should_show;
        cl_date_elt[i]->h_justify = -1;
        rr_ui_container_add_element(list, cl_date_elt[i]);
        cl_item_elt[i] = rr_ui_text_init(cl_text[i], 14, 0xffffffff);
        cl_item_elt[i]->should_show = cl_item_should_show;
        cl_item_elt[i]->h_justify = -1;
        rr_ui_container_add_element(list, cl_item_elt[i]);
    }
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
