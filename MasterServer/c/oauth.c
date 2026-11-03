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

// Discord OAuth: login/signup ("continue with Discord"), linking and
// unlinking. Google and Apple share the same shape and are routed to
// ms_h_provider_unavailable until credentials exist.
//
// Browser flow: GET /oauth/discord/start -> Discord -> GET
// /oauth/discord/callback -> 302 to CLIENT_URL#oauth_code=<one time code> ->
// the page POSTs the code to /oauth/exchange and receives either tokens or a
// pending signup that must be finished with /auth/complete-profile.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "discord.h"
#include "social.h"
#include "tmpstore.h"
#include "util.h"

static void redirect_client(struct ms_res *res, char const *key,
                            char const *value)
{
    char encoded[512];
    ms_url_encode(encoded, sizeof encoded, value);
    char url[768];
    snprintf(url, sizeof url, "%s#%s=%s", MS_CONFIG.client_url, key, encoded);
    ms_res_redirect(res, url);
}

static void sanitize_username(char *out, size_t size, char const *in)
{
    size_t o = 0;
    for (; *in && o + 1 < size && o < MS_USERNAME_MAX; ++in)
    {
        unsigned char c = (unsigned char)*in;
        if ((isalnum(c) && c < 128) || c == '_' || c == '.' || c == '-' ||
            c == ' ')
            out[o++] = (char)c;
    }
    out[o] = 0;
    // trim spaces
    while (o > 0 && out[o - 1] == ' ')
        out[--o] = 0;
    size_t start = 0;
    while (out[start] == ' ')
        ++start;
    if (start)
        memmove(out, out + start, strlen(out + start) + 1);
    if (strlen(out) < 3)
        out[0] = 0;
}

struct provider
{
    char const *name;
    int (*enabled)();
    int (*authorize_url)(char const *, char *, size_t);
    int (*exchange)(char const *, struct ms_discord_user *);
};

static struct provider const DISCORD = {"discord", ms_discord_oauth_enabled,
                                        ms_discord_authorize_url,
                                        ms_discord_exchange_code};
static struct provider const GOOGLE = {"google", ms_google_enabled,
                                       ms_google_authorize_url,
                                       ms_google_exchange_code};
static struct provider const APPLE = {"apple", ms_apple_enabled,
                                      ms_apple_authorize_url,
                                      ms_apple_exchange_code};

static void start_for(struct provider const *P, struct ms_req *req,
                      struct ms_res *res)
{
    if (!P->enabled() || MS_CONFIG.client_url[0] == 0)
    {
        if (MS_CONFIG.client_url[0])
            redirect_client(res, "oauth_error", "unavailable");
        else
            ms_res_error(res, 503, "provider_unavailable", NULL);
        return;
    }
    struct ms_tmp state;
    memset(&state, 0, sizeof state);
    state.kind = ms_tmp_oauth_state;
    ms_strlcpy(state.s1, "login", sizeof state.s1);
    char url[1024];
    if (ms_tmp_create(&state, 600) != 0 ||
        P->authorize_url(state.id, url, sizeof url) != 0)
    {
        redirect_client(res, "oauth_error", "server_error");
        return;
    }
    ms_res_redirect(res, url);
}

static void link_start_for(struct provider const *P, struct ms_req *req,
                           struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r =
        ms_auth_verify(ms_json_str(body, "uuid"), ms_json_str(body, "pl_t"),
                       ms_json_str(body, "pl_p"), &a);
    if (ms_json_str(body, "pl_t") == NULL)
        r = ms_auth_invalid;
    if (r != ms_auth_ok)
        ms_auth_res_failure(res, r);
    else if (!P->enabled())
        ms_res_error(res, 503, "provider_unavailable", NULL);
    else
    {
        struct ms_tmp state;
        memset(&state, 0, sizeof state);
        state.kind = ms_tmp_oauth_state;
        ms_strlcpy(state.s1, "link", sizeof state.s1);
        ms_strlcpy(state.uuid, a.uuid, sizeof state.uuid);
        char url[1024];
        if (ms_tmp_create(&state, 600) != 0 ||
            P->authorize_url(state.id, url, sizeof url) != 0)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            cJSON_AddStringToObject(o, "url", url);
            ms_res_json(res, 200, o);
        }
    }
    cJSON_Delete(body);
}

// `args` is the query (Discord, Google) or the form body (Apple's form_post)
static void callback_for(struct provider const *P, char const *args,
                         struct ms_res *res)
{
    if (MS_CONFIG.client_url[0] == 0)
    {
        ms_res_error(res, 503, "provider_unavailable", NULL);
        return;
    }
    char state_id[128], code[2048], error[64];
    if (ms_query_arg(args, "error", error, sizeof error))
    {
        redirect_client(res, "oauth_error", "denied");
        return;
    }
    if (!ms_query_arg(args, "state", state_id, sizeof state_id) ||
        !ms_query_arg(args, "code", code, sizeof code))
    {
        redirect_client(res, "oauth_error", "bad_request");
        return;
    }
    struct ms_tmp state;
    if (!ms_tmp_take(ms_tmp_oauth_state, state_id, &state))
    {
        redirect_client(res, "oauth_error", "expired");
        return;
    }
    struct ms_discord_user user;
    if (!P->enabled() || P->exchange(code, &user) != 0)
    {
        redirect_client(res, "oauth_error", "provider_failed");
        return;
    }

    if (strcmp(state.s1, "link") == 0)
    {
        struct ms_db_link link;
        memset(&link, 0, sizeof link);
        ms_strlcpy(link.provider, P->name, sizeof link.provider);
        ms_strlcpy(link.provider_user_id, user.id,
                   sizeof link.provider_user_id);
        ms_strlcpy(link.provider_email, user.email, sizeof link.provider_email);
        ms_strlcpy(link.display_name, user.username, sizeof link.display_name);
        link.linked_at = ms_now();
        int rc = MS_DB->link_put(MS_DB, state.uuid, &link);
        if (rc == MS_DB_OK)
            redirect_client(res, "oauth", "linked");
        else if (rc == MS_DB_CONFLICT_LINK)
            redirect_client(res, "oauth_error", "link_conflict");
        else
            redirect_client(res, "oauth_error", "server_error");
        return;
    }

    struct ms_tmp exchange;
    memset(&exchange, 0, sizeof exchange);
    exchange.kind = ms_tmp_exchange;
    struct ms_db_account existing;
    int rc = MS_DB->account_get(MS_DB, ms_db_by_link, P->name, user.id,
                                &existing);
    if (rc == MS_DB_OK)
    {
        ms_strlcpy(exchange.s1, "ok", sizeof exchange.s1);
        ms_strlcpy(exchange.uuid, existing.uuid, sizeof exchange.uuid);
    }
    else if (rc == MS_DB_NOT_FOUND)
    {
        struct ms_tmp pending;
        memset(&pending, 0, sizeof pending);
        pending.kind = ms_tmp_pending;
        ms_strlcpy(pending.s1, P->name, sizeof pending.s1);
        ms_strlcpy(pending.s2, user.id, sizeof pending.s2);
        ms_strlcpy(pending.s3, user.email, sizeof pending.s3);
        ms_strlcpy(pending.s4, user.username, sizeof pending.s4);
        if (ms_tmp_create(&pending, 900) != 0)
        {
            redirect_client(res, "oauth_error", "server_error");
            return;
        }
        ms_strlcpy(exchange.s1, "pending", sizeof exchange.s1);
        ms_strlcpy(exchange.s2, pending.id, sizeof exchange.s2);
    }
    else
    {
        redirect_client(res, "oauth_error", "server_error");
        return;
    }
    if (ms_tmp_create(&exchange, 120) != 0)
    {
        redirect_client(res, "oauth_error", "server_error");
        return;
    }
    redirect_client(res, "oauth_code", exchange.id);
}

void ms_h_oauth_exchange(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *code = ms_json_str(body, "code");
    struct ms_tmp exchange;
    if (code == NULL || !ms_tmp_take(ms_tmp_exchange, code, &exchange))
    {
        ms_res_error(res, 400, "expired", "Sign-in expired, please try again");
        cJSON_Delete(body);
        return;
    }
    if (strcmp(exchange.s1, "ok") == 0)
    {
        struct ms_db_account a;
        if (MS_DB->account_get(MS_DB, ms_db_by_uuid, exchange.uuid, NULL, &a) !=
            MS_DB_OK)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            a.last_login_at = ms_now();
            MS_DB->account_set_last_login(MS_DB, a.uuid, a.last_login_at);
            cJSON *o = ms_auth_login_json(&a);
            cJSON_AddStringToObject(o, "status", "ok");
            ms_res_json(res, 200, o);
        }
    }
    else
    {
        struct ms_tmp pending;
        memset(&pending, 0, sizeof pending);
        char suggestion[MS_USERNAME_MAX + 1] = "";
        if (ms_tmp_get(ms_tmp_pending, exchange.s2, &pending))
            sanitize_username(suggestion, sizeof suggestion, pending.s4);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "status", "profile_required");
        cJSON_AddStringToObject(o, "pending_id", exchange.s2);
        cJSON_AddStringToObject(o, "provider", pending.s1[0] ? pending.s1 : "discord");
        cJSON_AddStringToObject(o, "suggested_username", suggestion);
        ms_res_json(res, 200, o);
    }
    cJSON_Delete(body);
}

void ms_h_unlink(struct ms_req *req, struct ms_res *res)
{
    char const *provider = req->path + strlen("/auth/unlink/");
    if (strcmp(provider, "discord") != 0 && strcmp(provider, "google") != 0 &&
        strcmp(provider, "apple") != 0)
    {
        ms_res_error(res, 404, "not_found", NULL);
        return;
    }
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r =
        ms_auth_verify(ms_json_str(body, "uuid"), ms_json_str(body, "pl_t"),
                       ms_json_str(body, "pl_p"), &a);
    if (ms_json_str(body, "pl_t") == NULL)
        r = ms_auth_invalid;
    if (r != ms_auth_ok)
        ms_auth_res_failure(res, r);
    else if (MS_DB->link_delete(MS_DB, a.uuid, provider) != MS_DB_OK)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        ms_res_json(res, 200, o);
    }
    cJSON_Delete(body);
}

// ---- the routes

void ms_h_discord_start(struct ms_req *req, struct ms_res *res)
{
    start_for(&DISCORD, req, res);
}
void ms_h_discord_link_start(struct ms_req *req, struct ms_res *res)
{
    link_start_for(&DISCORD, req, res);
}
void ms_h_discord_callback(struct ms_req *req, struct ms_res *res)
{
    callback_for(&DISCORD, req->query, res);
}

void ms_h_google_start(struct ms_req *req, struct ms_res *res)
{
    start_for(&GOOGLE, req, res);
}
void ms_h_google_link_start(struct ms_req *req, struct ms_res *res)
{
    link_start_for(&GOOGLE, req, res);
}
void ms_h_google_callback(struct ms_req *req, struct ms_res *res)
{
    callback_for(&GOOGLE, req->query, res);
}

void ms_h_apple_start(struct ms_req *req, struct ms_res *res)
{
    start_for(&APPLE, req, res);
}
void ms_h_apple_link_start(struct ms_req *req, struct ms_res *res)
{
    link_start_for(&APPLE, req, res);
}
// Apple sends the browser back with a POST (response_mode=form_post): the code
// and the state are in the form body
void ms_h_apple_callback(struct ms_req *req, struct ms_res *res)
{
    char form[4096] = "";
    if (req->body != NULL && req->body_len < sizeof form)
    {
        memcpy(form, req->body, req->body_len);
        form[req->body_len] = 0;
    }
    callback_for(&APPLE, form, res);
}

// which providers can be used right now, for the login page
void ms_h_providers(struct ms_req *req, struct ms_res *res)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddBoolToObject(o, "discord", ms_discord_oauth_enabled());
    cJSON_AddBoolToObject(o, "google", ms_google_enabled());
    cJSON_AddBoolToObject(o, "apple", ms_apple_enabled());
    ms_res_json(res, 200, o);
}
