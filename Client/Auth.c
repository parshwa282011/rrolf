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

#include <Client/DOM.h>
#include <Client/Game.h>
#include <Shared/Api.h>

enum request_kind
{
    kind_session,
    kind_login,
    kind_signup,
    kind_reset_start,
    kind_reset_send,
    kind_reset_confirm,
    kind_exchange,
    kind_profile,
    kind_link_start
};

static void set_message(struct rr_game *game, char const *text)
{
    snprintf(game->auth.message, sizeof game->auth.message, "%s", text);
}

void rr_auth_show(struct rr_game *game, enum rr_auth_screen screen)
{
    game->auth.screen = screen;
    game->auth.busy = 0;
    game->auth.message[0] = 0;
}

// JSON string escaping for the request bodies (passwords can hold anything)
static void json_escape(char *out, size_t size, char const *in)
{
    size_t o = 0;
    for (; *in && o + 7 < size; ++in)
    {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\')
        {
            out[o++] = '\\';
            out[o++] = (char)c;
        }
        else if (c < 0x20)
            o += (size_t)snprintf(out + o, size - o, "\\u%04x", c);
        else
            out[o++] = (char)c;
    }
    out[o] = 0;
}

#ifdef EMSCRIPTEN
// POSTs `body` (or GETs when NULL) to the master and reports back through
// rr_auth_on_response. All JSON members are read with brackets so the closure
// compiler leaves them alone.
static void request(int kind, char const *path, char const *body)
{
    EM_ASM(
        {
            const kind = $0;
            const base = UTF8ToString($1);
            const path = UTF8ToString($2);
            let body = $3 ? UTF8ToString($3) : undefined;
            if (body === "@session")
                body = JSON.stringify({
                    "uuid" : window.localStorage["uuid"],
                    "pl_t" : window.localStorage["PL-T"],
                    "pl_p" : window.localStorage["PL-P"]
                });
            const S = function(v)
            {
                v = (v == null) ? "" : String(v);
                const b = new TextEncoder().encode(v);
                const p = _malloc(b.length + 1);
                HEAPU8.set(b, p);
                HEAPU8[p + b.length] = 0;
                return p;
            };
            fetch(base + path, {
                "method" : body !== undefined ? "POST" : "GET",
                "headers" : {"content-type" : "application/json"},
                "body" : body
            })
                .then(function(r) {
                    return r.json().then(function(j) { return [ r.status, j ]; });
                })
                .catch(function(e) { return [ 0, {} ]; })
                .then(function(res) {
                    const status = res[0];
                    const j = res[1] || {};
                    if (kind == 8 && j["url"])
                    {
                        window.location.href = j["url"];
                        return;
                    }
                    const stored = status >= 200 && status < 300 &&
                                   j["pl_t"] && j["pl_p"] && j["uuid"];
                    if (stored)
                    {
                        const owner = window.localStorage["rrolf_cache_owner"];
                        window.localStorage["PL-T"] = j["pl_t"];
                        window.localStorage["PL-P"] = j["pl_p"];
                        window.localStorage["uuid"] = j["uuid"];
                        window.localStorage["rrolf_cache_owner"] = j["uuid"];
                        if (owner && owner !== j["uuid"])
                        {
                            // another account used this browser: do not mix
                            // their cached loadout / mob gallery into this one
                            delete window.localStorage["rrolf_account_data"];
                            window.location.reload();
                            return;
                        }
                    }
                    if ((kind == 0) && (status == 401 || status == 403))
                    {
                        delete window.localStorage["PL-T"];
                        delete window.localStorage["PL-P"];
                    }
                    let methods = "";
                    if (j["methods"])
                        methods = j["methods"]
                                      .filter(function(m) { return m["available"]; })
                                      .map(function(m) { return m["method"]; })
                                      .join(",");
                    const args = [
                        kind,
                        status,
                        S(j["error"]),
                        S(j["message"]),
                        S(j["uuid"] || (kind == 0 ? window.localStorage["uuid"] : "")),
                        S(j["pl_t"]),
                        S(j["pl_p"] || (kind == 0 ? window.localStorage["PL-P"] : "")),
                        S(j["username"]),
                        S(j["reset_id"] || j["pending_id"]),
                        S(j["suggested_username"] || methods),
                        S(j["notice"] || j["status"])
                    ];
                    _rr_auth_on_response(args[0], args[1], args[2], args[3],
                                         args[4], args[5], args[6], args[7],
                                         args[8], args[9], args[10]);
                    for (let i = 2; i < args.length; ++i)
                        _free(args[i]);
                });
        },
        kind, RR_BASE_API_URL, path, body);
}
#else
static void request(int kind, char const *path, char const *body) {}
#endif

static struct rr_game *auth_game;

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
    memset(game->auth.password, 0, sizeof game->auth.password);
    memset(game->auth.new_password, 0, sizeof game->auth.new_password);
#ifdef EMSCRIPTEN
    rr_dom_set_text("auth_password", "");
    rr_dom_set_text("auth_signup_password", "");
    rr_dom_set_text("auth_profile_password", "");
    rr_dom_set_text("auth_new_password", "");
#endif
    rr_auth_show(game, rr_auth_screen_none);
    rr_game_connect_socket(game);
}

static char const *describe_error(char const *code, char const *message)
{
    if (message[0])
        return message;
    if (!strcmp(code, "invalid_credentials"))
        return "Wrong username or password";
    if (!strcmp(code, "locked"))
        return "Too many failed attempts, try again later";
    if (!strcmp(code, "rate_limited"))
        return "Too many requests, slow down";
    if (!strcmp(code, "expired"))
        return "Your session expired, please log in again";
    if (!strcmp(code, "provider_unavailable"))
        return "Unavailable: the email service is not working right now";
    if (code[0])
        return code;
    return "Could not reach the account server";
}

void rr_auth_on_response(int kind, int status, char *error, char *message,
                         char *uuid, char *pl_t, char *pl_p, char *username,
                         char *extra1, char *extra2, char *extra3)
{
    struct rr_game *game = auth_game;
    if (game == NULL)
        return;
    game->auth.busy = 0;
    int ok = status >= 200 && status < 300;

    switch (kind)
    {
    case kind_session:
        if (ok && uuid[0] && pl_p[0])
            finish_login(game, uuid, username[0] ? username : "player", pl_p);
        else
        {
            rr_auth_show(game, rr_auth_screen_login);
            if (status == 401)
                set_message(game, strcmp(error, "expired") == 0
                                      ? "Your session expired, please log in "
                                        "again"
                                      : "Please log in");
            else if (status == 0)
                set_message(game, "Could not reach the account server");
        }
        break;
    case kind_login:
    case kind_signup:
    case kind_profile:
        if (ok && uuid[0] && pl_p[0])
            finish_login(game, uuid, username, pl_p);
        else
            set_message(game, describe_error(error, message));
        break;
    case kind_exchange:
        if (ok && uuid[0] && pl_p[0])
            finish_login(game, uuid, username, pl_p);
        else if (ok && !strcmp(extra3, "profile_required"))
        {
            snprintf(game->auth.pending_id, sizeof game->auth.pending_id, "%s",
                     extra1);
            snprintf(game->auth.username, sizeof game->auth.username, "%s",
                     extra2);
#ifdef EMSCRIPTEN
            rr_dom_set_text("auth_username", game->auth.username);
#endif
            rr_auth_show(game, rr_auth_screen_profile);
            set_message(game, "Choose a username and password for your account");
        }
        else
        {
            rr_auth_show(game, rr_auth_screen_login);
            set_message(game, describe_error(error, message));
        }
        break;
    case kind_reset_start:
        if (ok)
        {
            snprintf(game->auth.reset_id, sizeof game->auth.reset_id, "%s",
                     extra1);
            game->auth.reset_discord_available = strstr(extra2, "discord") != 0;
            game->auth.reset_code_sent = 0;
            snprintf(game->auth.notice, sizeof game->auth.notice, "%s", extra3);
            rr_auth_show(game, rr_auth_screen_reset_code);
        }
        else
            set_message(game, describe_error(error, message));
        break;
    case kind_reset_send:
        if (ok)
        {
            game->auth.reset_code_sent = 1;
            set_message(game, "Code sent. Check your Discord messages.");
        }
        else
            set_message(game, describe_error(error, message));
        break;
    case kind_reset_confirm:
        if (ok)
        {
            rr_auth_show(game, rr_auth_screen_login);
            set_message(game, "Password changed. Log in with your new password.");
        }
        else
            set_message(game, describe_error(error, message));
        break;
    default:
        break;
    }
}

void rr_auth_start(struct rr_game *game)
{
    auth_game = game;
#ifdef EMSCRIPTEN
    // a Discord sign-in returns here as #oauth_code=<one time code>
    char code[128] = "";
    char problem[64] = "";
    int returned = EM_ASM_INT(
        {
            const h = window.location.hash || "";
            const write = function(s, p)
            {
                const b = new TextEncoder().encode(s);
                HEAPU8.set(b, p);
                HEAPU8[p + b.length] = 0;
            };
            const find = function(key)
            {
                const i = h.indexOf(key + "=");
                if (i < 0)
                    return null;
                let v = h.substring(i + key.length + 1);
                const amp = v.indexOf("&");
                if (amp >= 0)
                    v = v.substring(0, amp);
                return decodeURIComponent(v).substring(0, 100);
            };
            const c = find("oauth_code");
            const e = find("oauth_error");
            if (c || e || h.indexOf("oauth=linked") >= 0)
                window.history.replaceState(null, "",
                                            window.location.pathname +
                                                window.location.search);
            if (c)
            {
                write(c, $0);
                return 1;
            }
            if (e)
            {
                write(e, $1);
                return 2;
            }
            return 0;
        },
        code, problem);
    if (returned == 1)
    {
        char body[256];
        char escaped[256];
        json_escape(escaped, sizeof escaped, code);
        snprintf(body, sizeof body, "{\"code\":\"%s\"}", escaped);
        game->auth.screen = rr_auth_screen_checking;
        game->auth.busy = 1;
        request(kind_exchange, "oauth/exchange", body);
        return;
    }
    int have_session = EM_ASM_INT({
        return window.localStorage["uuid"] && window.localStorage["PL-T"] &&
               window.localStorage["PL-P"]
                   ? 1
                   : 0;
    });
    if (have_session)
    {
        // PL-T + PL-P go to the master only; the game server gets the PL-P
        game->auth.screen = rr_auth_screen_checking;
        game->auth.busy = 1;
        request(kind_session, "auth/session", "@session");
        return;
    }
    rr_auth_show(game, rr_auth_screen_login);
    if (returned == 2)
        set_message(game, strcmp(problem, "unavailable") == 0
                              ? "Discord sign-in is not set up on this server"
                              : "Discord sign-in did not work, try again");
#else
    // native debug build: no browser, no accounts
    finish_login(game, "00000000-0000-4000-8000-000000000000", "native", "");
#endif
}

#define REQUIRE_IDLE(game)                                                     \
    if ((game)->auth.busy)                                                     \
        return;                                                                \
    (game)->auth.busy = 1;                                                     \
    (game)->auth.message[0] = 0;

void rr_auth_login(struct rr_game *game)
{
    if (game->auth.identifier[0] == 0 || game->auth.password[0] == 0)
    {
        set_message(game, "Enter your username (or email) and password");
        return;
    }
    REQUIRE_IDLE(game)
    char a[600], b[300], body[1000];
    json_escape(a, sizeof a, game->auth.identifier);
    json_escape(b, sizeof b, game->auth.password);
    snprintf(body, sizeof body, "{\"identifier\":\"%s\",\"password\":\"%s\"}", a,
             b);
    request(kind_login, "auth/login", body);
}

void rr_auth_signup(struct rr_game *game)
{
    if (game->auth.email[0] == 0 || game->auth.username[0] == 0 ||
        game->auth.password[0] == 0)
    {
        set_message(game, "Fill in your email, username and password");
        return;
    }
    REQUIRE_IDLE(game)
    char e[600], u[300], p[300], body[1400];
    json_escape(e, sizeof e, game->auth.email);
    json_escape(u, sizeof u, game->auth.username);
    json_escape(p, sizeof p, game->auth.password);
    snprintf(body, sizeof body,
             "{\"email\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}", e, u,
             p);
    request(kind_signup, "auth/signup", body);
}

void rr_auth_complete_profile(struct rr_game *game)
{
    if (game->auth.username[0] == 0 || game->auth.password[0] == 0)
    {
        set_message(game, "Choose a username and a password");
        return;
    }
    REQUIRE_IDLE(game)
    char id[200], u[300], p[300], body[1000];
    json_escape(id, sizeof id, game->auth.pending_id);
    json_escape(u, sizeof u, game->auth.username);
    json_escape(p, sizeof p, game->auth.password);
    snprintf(body, sizeof body,
             "{\"pending_id\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}",
             id, u, p);
    request(kind_profile, "auth/complete-profile", body);
}

void rr_auth_discord(struct rr_game *game)
{
#ifdef EMSCRIPTEN
    EM_ASM({ window.location.href = UTF8ToString($0) + "oauth/discord/start"; },
           RR_BASE_API_URL);
#endif
}

void rr_auth_link_discord(struct rr_game *game)
{
#ifdef EMSCRIPTEN
    // the master answers with the Discord URL for this account
    EM_ASM(
        {
            const base = UTF8ToString($0);
            fetch(base + "oauth/discord/link-start", {
                "method" : "POST",
                "headers" : {"content-type" : "application/json"},
                "body" : JSON.stringify({
                    "uuid" : window.localStorage["uuid"],
                    "pl_t" : window.localStorage["PL-T"],
                    "pl_p" : window.localStorage["PL-P"]
                })
            })
                .then(function(r) { return r.json(); })
                .then(function(j) {
                    if (j["url"])
                        window.location.href = j["url"];
                });
        },
        RR_BASE_API_URL);
#endif
}

void rr_auth_reset_start(struct rr_game *game)
{
    if (game->auth.identifier[0] == 0)
    {
        set_message(game, "Enter your username or email");
        return;
    }
    REQUIRE_IDLE(game)
    char a[600], body[700];
    json_escape(a, sizeof a, game->auth.identifier);
    snprintf(body, sizeof body, "{\"identifier\":\"%s\"}", a);
    request(kind_reset_start, "auth/reset/start", body);
}

void rr_auth_reset_send(struct rr_game *game)
{
    if (!game->auth.reset_discord_available)
    {
        set_message(game, "Discord is not linked to this account");
        return;
    }
    REQUIRE_IDLE(game)
    char id[200], body[400];
    json_escape(id, sizeof id, game->auth.reset_id);
    snprintf(body, sizeof body, "{\"reset_id\":\"%s\",\"method\":\"discord\"}",
             id);
    request(kind_reset_send, "auth/reset/send", body);
}

void rr_auth_reset_confirm(struct rr_game *game)
{
    if (game->auth.otp[0] == 0 || game->auth.new_password[0] == 0)
    {
        set_message(game, "Enter the code and your new password");
        return;
    }
    REQUIRE_IDLE(game)
    char id[200], otp[64], pw[300], body[800];
    json_escape(id, sizeof id, game->auth.reset_id);
    json_escape(otp, sizeof otp, game->auth.otp);
    json_escape(pw, sizeof pw, game->auth.new_password);
    snprintf(body, sizeof body,
             "{\"reset_id\":\"%s\",\"otp\":\"%s\",\"new_password\":\"%s\"}", id,
             otp, pw);
    request(kind_reset_confirm, "auth/reset/confirm", body);
}

void rr_auth_logout(struct rr_game *game)
{
#ifdef EMSCRIPTEN
    EM_ASM({
        delete window.localStorage["PL-T"];
        delete window.localStorage["PL-P"];
        delete window.localStorage["uuid"];
        window.location.reload();
    });
#endif
}
