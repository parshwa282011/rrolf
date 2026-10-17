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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/Game.h>
#include <Client/Platform.h>
#include <Client/Storage.h>

// Account requests go straight to the master server's public /auth endpoints
// (platform.js, platform.auth), not through a game server. path is "/link/
// discord", "/link/crazygames", "/code/create" or "/code/redeem"; field and
// value add one string to the request body (field may be NULL).
// connect_anyway reconnects to the game server even if the request fails,
// which the first connect of a page load needs.
void rr_auth_post(void *game, char const *path, char const *field,
                  char const *value, uint8_t connect_anyway)
{
    struct rr_game *this = game;
    EM_ASM({
        const put = function(string) {
            const bytes = new TextEncoder().encode(string);
            const pointer = _malloc(bytes.length + 1);
            HEAPU8.set(bytes, pointer);
            HEAPU8[pointer + bytes.length] = 0;
            return pointer;
        };
        const finish = function(flags, uuid, token, code, message) {
            _rr_auth_on_result(put(uuid), put(token), put(code), put(message),
                               flags, $0);
        };
        const platform = window["rrolfPlatform"];
        const retry = $6 ? 2 : 0;
        if (!platform) {
            finish(retry, "", "", "", "Accounts are not available here.");
            return;
        }
        // no comma outside of parentheses in here, EM_ASM would split on it
        const body = {};
        body["uuid"] = UTF8ToString($4);
        body["token"] = UTF8ToString($5);
        if ($2)
            body[UTF8ToString($2)] = UTF8ToString($3);
        platform["auth"](UTF8ToString($1), body).then(function(result) {
            const ok = result["ok"] === true;
            finish(retry | (ok ? 1 : 0),
                   ok ? String(result["uuid"] || "") : "",
                   ok ? String(result["token"] || "") : "",
                   ok ? String(result["code"] || "") : "",
                   String(result["message"] || ""));
        });
    }, this, path, field, value, this->rivet_account.uuid,
       this->rivet_account.token, connect_anyway);
}

// Called from JS with malloc'ed strings, which it frees. flags: 1 the request
// worked, 2 reconnect even if it did not. A reply with an account (uuid and
// token) reconnects with it; a transfer code reply only shows the code.
void rr_auth_on_result(char *uuid, char *token, char *code, char *message,
                       int flags, void *game)
{
    struct rr_game *this = game;
    snprintf(this->auth_message, sizeof this->auth_message, "%s", message);
    if (code[0])
        snprintf(this->transfer_code, sizeof this->transfer_code, "%s", code);
    uint8_t new_account = (flags & 1) && uuid[0] && token[0] &&
                          strlen(uuid) < sizeof this->rivet_account.uuid &&
                          strlen(token) < sizeof this->rivet_account.token;
    if (new_account)
    {
        strcpy(this->rivet_account.uuid, uuid);
        strcpy(this->rivet_account.token, token);
    }
    free(uuid);
    free(token);
    free(code);
    free(message);
    if (new_account || (flags & 2))
        rr_game_connect_socket(this);
}

void rr_auth_get_transfer_code(void *game)
{
    struct rr_game *this = game;
    this->transfer_code[0] = 0;
    snprintf(this->auth_message, sizeof this->auth_message, "Making a code...");
    rr_auth_post(game, "/code/create", NULL, NULL, 0);
}

void rr_auth_use_transfer_code(void *game)
{
    struct rr_game *this = game;
    if (this->enter_code[0] == 0)
    {
        snprintf(this->auth_message, sizeof this->auth_message,
                 "Type the code first.");
        return;
    }
    snprintf(this->auth_message, sizeof this->auth_message, "Checking...");
    rr_auth_post(game, "/code/redeem", "code", this->enter_code, 0);
}

void rr_discord_oauth2_read_data(void *_this, void *_decoder) {
    struct rr_game *this = _this;
    struct proto_bug *decoder = _decoder;
    proto_bug_read_string(decoder, this->rivet_account.uuid,
                          sizeof this->rivet_account.uuid, "uuid");
    proto_bug_read_string(decoder, this->rivet_account.token,
                          sizeof this->rivet_account.token, "token");
    proto_bug_read_string(decoder, this->rivet_account.name,
                          sizeof this->rivet_account.name, "name");
    this->rivet_account.code[0] = 0;
    this->rivet_account.cg_token[0] = 0;
    this->account_linked = this->rivet_account.name[0] != 0;
    if (!this->account_linked)
        strcpy(this->rivet_account.name, "Guest");
    rr_local_storage_store_string("rivet_account_uuid", this->rivet_account.uuid);
    rr_local_storage_store_string("DO_NOT_SHARE_account_token", this->rivet_account.token);
}

void rr_discord_oauth2_link_account() {
    EM_ASM({
        const state = crypto.randomUUID();
        const url = new URL("https://discord.com/oauth2/authorize");
        url.searchParams.set("client_id", "1453525695228678349");
        url.searchParams.set("response_type", "code");
        url.searchParams.set("redirect_uri", window.location.origin + window.location.pathname);
        url.searchParams.set("scope", "identify guilds.join");
        url.searchParams.set("state", state);
        window.localStorage["oauth2_state"] = state;
        window.onbeforeunload = null;
        window.location.href = url;
    });
}

void rr_discord_oauth2_on_log_in(char *uuid, char *token, char *code, void *captures) {
    struct rr_game *this = captures;
    strcpy(this->rivet_account.uuid, uuid);
    strcpy(this->rivet_account.token, token);
    // the code only comes back from a Discord login. It is exchanged at the
    // master server, then the game server is connected to with the account
    // that gives, so the game server never sees a login code.
    uint8_t has_code = code[0] != 0 && uuid[0] != 0;
    if (has_code)
        rr_auth_post(this, "/link/discord", "code", code, 1);
    free(uuid);
    free(token);
    free(code);
    if (!has_code)
        rr_game_connect_socket(this);
}

void rr_discord_oauth2_init(void *captures) {
    EM_ASM({
        const url = new URL(window.location);
        const uuid = url.searchParams.get("uuid") || window.localStorage["rivet_account_uuid"] || "";
        const token = window.localStorage["DO_NOT_SHARE_account_token"] || "";
        const _code = url.searchParams.get("code") || "";
        const _state = url.searchParams.get("state") || "";
        const state = window.localStorage["oauth2_state"];
        const code = (state && _state && state === _state) ? _code : "";
        url.searchParams.delete("uuid");
        url.searchParams.delete("code");
        url.searchParams.delete("state");
        url.searchParams.delete("error");
        url.searchParams.delete("error_description");
        window.history.replaceState(null, null, url);
        delete window.localStorage["oauth2_state"];
        const $uuid = _malloc(uuid.length + 1);
        const $token = _malloc(token.length + 1);
        const $code = _malloc(code.length + 1);
        HEAPU8.set(new TextEncoder().encode(uuid), $uuid);
        HEAPU8.set(new TextEncoder().encode(token), $token);
        HEAPU8.set(new TextEncoder().encode(code), $code);
        HEAPU8[$uuid + uuid.length] = 0;
        HEAPU8[$token + token.length] = 0;
        HEAPU8[$code + code.length] = 0;
        _rr_discord_oauth2_on_log_in($uuid, $token, $code, $0);
    }, captures);
}
