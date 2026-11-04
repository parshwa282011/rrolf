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

#include <Client/Auth.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include <Client/Game.h>
#include <Shared/Api.h>

static struct rr_game *auth_game;

static void set_message(struct rr_game *game, char const *text)
{
    snprintf(game->auth.message, sizeof game->auth.message, "%s", text);
}

#ifdef EMSCRIPTEN
// Sends the browser to the log in page, which brings it back here afterwards.
// `hash` is passed on (a Discord/Google/Apple sign in returns with
// #oauth_code=... and the page finishes it)
static void go_to_login(char const *hash)
{
    EM_ASM(
        {
            const hash = UTF8ToString($0);
            const next = window.location.pathname + window.location.search;
            window.location.replace("/login?next=" + encodeURIComponent(next) +
                                    hash);
        },
        hash);
}

// Asks the master whether the stored login still holds. A login that does not
// (expired, other password, email not confirmed yet) goes to the log in page;
// only a working one, or no answer at all, comes back to rr_auth_on_response.
static void check_session()
{
    EM_ASM(
        {
            const base = UTF8ToString($0);
            const S = function(v)
            {
                v = (v == null) ? "" : String(v);
                const b = new TextEncoder().encode(v);
                const p = _malloc(b.length + 1);
                HEAPU8.set(b, p);
                HEAPU8[p + b.length] = 0;
                return p;
            };
            const toLogin = function()
            {
                delete window.localStorage["PL-T"];
                delete window.localStorage["PL-P"];
                window.location.replace(
                    "/login?next=" +
                    encodeURIComponent(window.location.pathname +
                                       window.location.search));
            };
            fetch(base + "auth/session", {
                "method" : "POST",
                "headers" : {"content-type" : "application/json"},
                "body" : JSON.stringify({
                    "uuid" : window.localStorage["uuid"],
                    "pl_t" : window.localStorage["PL-T"],
                    "pl_p" : window.localStorage["PL-P"]
                })
            })
                .then(function(r) {
                    return r.json().then(function(j) { return [ r.status, j ]; });
                })
                .catch(function(e) { return [ 0, {} ]; })
                .then(function(res) {
                    const status = res[0];
                    const j = res[1] || {};
                    if (status == 401 || status == 403)
                    {
                        toLogin();
                        return;
                    }
                    const args = [
                        status, S(j["uuid"] || window.localStorage["uuid"]),
                        S(window.localStorage["PL-P"]), S(j["username"])
                    ];
                    _rr_auth_on_response(args[0], args[1], args[2], args[3]);
                    for (let i = 1; i < args.length; ++i)
                        _free(args[i]);
                });
        },
        RR_BASE_API_URL);
}
#endif

static void finish_login(struct rr_game *game, char const *uuid,
                         char const *username, char const *pl_p)
{
    snprintf(game->rivet_account.uuid, sizeof game->rivet_account.uuid, "%s",
             uuid);
    snprintf(game->rivet_account.name, sizeof game->rivet_account.name, "%s",
             username);
    // the game server receives the PL-P where the rivet token used to be
    snprintf(game->rivet_player_token, sizeof game->rivet_player_token, "%s",
             pl_p);
    game->account_linked = 1;
    game->logged_in = 1;
    game->account_synced = 0;
    game->auth.screen = rr_auth_screen_none;
    game->auth.message[0] = 0;
    rr_game_connect_socket(game);
}

void rr_auth_on_response(int status, char *uuid, char *pl_p, char *username)
{
    struct rr_game *game = auth_game;
    if (game == NULL)
        return;
    if (status >= 200 && status < 300 && uuid[0] && pl_p[0])
        finish_login(game, uuid, username[0] ? username : "player", pl_p);
    else
        // the master could not be reached (the log in page would not help)
        set_message(game, "Could not reach the account server. Reload the "
                          "page to try again.");
}

void rr_auth_start(struct rr_game *game)
{
    auth_game = game;
#ifdef EMSCRIPTEN
    // a Discord/Google/Apple sign in returns here as #oauth_code=<one time
    // code> (or #oauth_error=...): the log in page finishes it. A finished
    // account link (#oauth=linked) is only taken out of the address.
    int to_login = EM_ASM_INT({
        const h = window.location.hash || "";
        if (h.indexOf("oauth_code=") >= 0 || h.indexOf("oauth_error=") >= 0)
            return 1;
        if (h.indexOf("oauth=linked") >= 0)
            window.history.replaceState(null, "", window.location.pathname +
                                                      window.location.search);
        return 0;
    });
    if (to_login)
    {
        char hash[256] = "";
        EM_ASM({
            const h = window.location.hash || "";
            const b = new TextEncoder().encode(h.substring(0, 200));
            HEAPU8.set(b, $0);
            HEAPU8[$0 + b.length] = 0;
        }, hash);
        go_to_login(hash);
        game->auth.screen = rr_auth_screen_checking;
        return;
    }
    int have_session = EM_ASM_INT({
        return window.localStorage["uuid"] && window.localStorage["PL-T"] &&
               window.localStorage["PL-P"]
                   ? 1
                   : 0;
    });
    game->auth.screen = rr_auth_screen_checking;
    if (have_session)
        // PL-T + PL-P go to the master only; the game server gets the PL-P
        check_session();
    else
        go_to_login("");
#else
    // native debug build: no browser, no accounts
    finish_login(game, "00000000-0000-4000-8000-000000000000", "native", "");
#endif
}

void rr_auth_link_discord(struct rr_game *game)
{
#ifdef EMSCRIPTEN
    // linking Discord, Google or Apple, an email, deleting the account: the account page
    EM_ASM({ window.location.href = "/account"; });
#endif
}

void rr_auth_logout(struct rr_game *game)
{
#ifdef EMSCRIPTEN
    EM_ASM({
        delete window.localStorage["PL-T"];
        delete window.localStorage["PL-P"];
        delete window.localStorage["uuid"];
        window.location.replace("/login");
    });
#endif
}
