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

#include "api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ratelimit.h"
#include "util.h"

struct ms_db *MS_DB;

enum route_kind
{
    route_public,  // browser facing, JSON, CORS allow-list
    route_server,  // game/gardn servers only, X-Server-Secret, no CORS
    route_clone    // backup servers only, X-Clone-Secret, no CORS
};

struct route
{
    char const *method;
    char const *path;
    int prefix; // path is a prefix
    ms_handler handler;
    enum route_kind kind;
    char const *limit_name; // per-ip bucket name
    uint32_t limit;
    uint32_t window;
};

static struct route const ROUTES[] = {
    {"GET", "/health", 0, ms_h_health, route_public, "health", 120, 60},
    {"GET", "/backup", 0, ms_h_backup, route_clone, NULL, 0, 0},

    {"POST", "/auth/signup", 0, ms_h_signup, route_public, "signup", 5, 3600},
    {"POST", "/auth/login", 0, ms_h_login, route_public, "login", 10, 600},
    {"POST", "/auth/session", 0, ms_h_session, route_public, "session", 60, 60},
    {"POST", "/auth/logout-all", 0, ms_h_logout_all, route_public, "logout",
     10, 600},
    {"POST", "/auth/account", 0, ms_h_account_info, route_public, "account",
     30, 60},
    {"POST", "/auth/complete-profile", 0, ms_h_complete_profile, route_public,
     "profile", 10, 600},

    {"POST", "/auth/reset/start", 0, ms_h_reset_start, route_public,
     "reset_start", 5, 600},
    {"POST", "/auth/reset/send", 0, ms_h_reset_send, route_public, "reset_send",
     5, 600},
    {"POST", "/auth/reset/confirm", 0, ms_h_reset_confirm, route_public,
     "reset_confirm", 10, 600},

    {"GET", "/oauth/discord/start", 0, ms_h_discord_start, route_public,
     "oauth_start", 20, 600},
    {"GET", "/oauth/discord/callback", 0, ms_h_discord_callback, route_public,
     "oauth_callback", 30, 600},
    {"POST", "/oauth/discord/link-start", 0, ms_h_discord_link_start,
     route_public, "oauth_link", 10, 600},
    {"POST", "/oauth/exchange", 0, ms_h_oauth_exchange, route_public,
     "oauth_exchange", 20, 600},
    {"POST", "/auth/unlink/", 1, ms_h_unlink, route_public, "unlink", 10, 600},

    // present but unavailable for now: google, apple and email
    {"GET", "/oauth/google/", 1, ms_h_provider_unavailable, route_public,
     "unavailable", 30, 60},
    {"POST", "/oauth/google/", 1, ms_h_provider_unavailable, route_public,
     "unavailable", 30, 60},
    {"GET", "/oauth/apple/", 1, ms_h_provider_unavailable, route_public,
     "unavailable", 30, 60},
    {"POST", "/oauth/apple/", 1, ms_h_provider_unavailable, route_public,
     "unavailable", 30, 60},
    {"POST", "/auth/email/", 1, ms_h_provider_unavailable, route_public,
     "unavailable", 30, 60},

    {"POST", "/srv/verify", 0, ms_h_srv_verify, route_server, NULL, 0, 0},
    {"POST", "/srv/gardn/load", 0, ms_h_srv_gardn_load, route_server, NULL, 0,
     0},
    {"POST", "/srv/gardn/flush", 0, ms_h_srv_gardn_flush, route_server, NULL, 0,
     0},
    {"POST", "/srv/leaderboard/submit", 0, ms_h_srv_lb_submit, route_server,
     NULL, 0, 0},
    {"GET", "/srv/leaderboard/top", 0, ms_h_srv_lb_top, route_server, NULL, 0,
     0},
};

int ms_origin_allowed(char const *origin)
{
    if (origin == NULL || origin[0] == 0)
        return 0;
    char const *p = MS_CONFIG.allowed_origins;
    size_t olen = strlen(origin);
    while (*p)
    {
        while (*p == ',' || isspace((unsigned char)*p))
            ++p;
        char const *end = p;
        while (*end && *end != ',')
            ++end;
        size_t len = (size_t)(end - p);
        while (len > 0 && isspace((unsigned char)p[len - 1]))
            --len;
        if (len == olen && strncmp(p, origin, len) == 0)
            return 1;
        p = end;
    }
    return 0;
}

void ms_res_free(struct ms_res *res)
{
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

void ms_res_json(struct ms_res *res, int status, cJSON *obj)
{
    ms_res_free(res);
    res->status = status;
    res->body = cJSON_PrintUnformatted(obj);
    res->body_len = res->body ? strlen(res->body) : 0;
    ms_strlcpy(res->content_type, "application/json", sizeof res->content_type);
    cJSON_Delete(obj);
}

void ms_res_error(struct ms_res *res, int status, char const *code,
                  char const *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "error", code);
    if (message != NULL)
        cJSON_AddStringToObject(o, "message", message);
    ms_res_json(res, status, o);
}

void ms_res_redirect(struct ms_res *res, char const *url)
{
    ms_res_free(res);
    res->status = 302;
    ms_strlcpy(res->location, url, sizeof res->location);
}

cJSON *ms_req_json(struct ms_req const *req)
{
    if (req->body == NULL || req->body_len == 0)
        return NULL;
    cJSON *o = cJSON_ParseWithLength(req->body, req->body_len);
    if (o != NULL && !cJSON_IsObject(o))
    {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

char const *ms_json_str(cJSON const *obj, char const *key)
{
    if (obj == NULL)
        return NULL;
    cJSON const *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int ms_query_arg(char const *query, char const *name, char *out, size_t size)
{
    size_t nlen = strlen(name);
    char const *p = query;
    while (*p)
    {
        char const *end = strchr(p, '&');
        size_t seg = end ? (size_t)(end - p) : strlen(p);
        if (seg > nlen && strncmp(p, name, nlen) == 0 && p[nlen] == '=')
        {
            size_t o = 0;
            for (char const *c = p + nlen + 1; c < p + seg; ++c)
            {
                char ch = *c;
                if (ch == '+')
                    ch = ' ';
                else if (ch == '%' && c + 2 < p + seg && hex_digit(c[1]) >= 0 &&
                         hex_digit(c[2]) >= 0)
                {
                    ch = (char)(hex_digit(c[1]) << 4 | hex_digit(c[2]));
                    c += 2;
                }
                if (o + 1 >= size)
                    break;
                out[o++] = ch;
            }
            out[o] = 0;
            return 1;
        }
        if (end == NULL)
            break;
        p = end + 1;
    }
    out[0] = 0;
    return 0;
}

void ms_h_health(struct ms_req *req, struct ms_res *res)
{
    int ok = MS_DB != NULL && MS_DB->ping(MS_DB) == MS_DB_OK;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "db", MS_DB ? MS_DB->name : "none");
    // which machine answered, and whether it can feed backup servers
    if (MS_CONFIG.node_name[0])
        cJSON_AddStringToObject(o, "node", MS_CONFIG.node_name);
    cJSON_AddBoolToObject(o, "backup",
                          MS_DB != NULL && MS_DB->backup_pull != NULL &&
                              strlen(MS_CONFIG.clone_secret) >= 16);
    ms_res_json(res, ok ? 200 : 503, o);
}

static struct route const *find_route(struct ms_req const *req)
{
    for (size_t i = 0; i < sizeof ROUTES / sizeof *ROUTES; ++i)
    {
        struct route const *r = &ROUTES[i];
        if (strcmp(r->method, req->method) != 0)
            continue;
        if (r->prefix ? strncmp(req->path, r->path, strlen(r->path)) == 0
                      : strcmp(req->path, r->path) == 0)
            return r;
    }
    return NULL;
}

static void apply_cors(struct ms_req const *req, struct ms_res *res)
{
    if (req->origin[0] && ms_origin_allowed(req->origin))
        ms_strlcpy(res->allow_origin, req->origin, sizeof res->allow_origin);
}

void ms_route(struct ms_req *req, struct ms_res *res)
{
    memset(res, 0, sizeof *res);

    if (strcmp(req->method, "OPTIONS") == 0)
    {
        // CORS preflight
        apply_cors(req, res);
        res->status = res->allow_origin[0] ? 204 : 403;
        return;
    }

    struct route const *route = find_route(req);
    if (route == NULL)
    {
        apply_cors(req, res);
        ms_res_error(res, 404, "not_found", NULL);
        return;
    }

    char key[128];
    if (route->kind == route_server || route->kind == route_clone)
    {
        int clone = route->kind == route_clone;
        char const *secret =
            clone ? MS_CONFIG.clone_secret : MS_CONFIG.server_secret;
        // never reachable from a browser
        if (req->origin[0])
        {
            ms_res_error(res, 403, "forbidden", NULL);
            return;
        }
        if (clone && strlen(secret) < 16)
        {
            ms_res_error(res, 503, "backup_disabled", "CLONE_SECRET is not set");
            return;
        }
        snprintf(key, sizeof key, "badsecret:%s", req->ip);
        if (ms_rl_blocked(key, 20, 60))
        {
            ms_res_error(res, 429, "rate_limited", NULL);
            return;
        }
        if (!ms_ct_str_equal(clone ? req->clone_secret : req->server_secret,
                             secret))
        {
            ms_rl_add(key, 60);
            ms_res_error(res, 403, "forbidden", NULL);
            return;
        }
        route->handler(req, res);
        return;
    }

    if (req->origin[0])
    {
        if (!ms_origin_allowed(req->origin))
        {
            ms_res_error(res, 403, "origin_not_allowed", NULL);
            return;
        }
        apply_cors(req, res);
    }
    snprintf(key, sizeof key, "gen:%s", req->ip);
    if (!ms_rl_hit(key, 240, 60))
    {
        ms_res_error(res, 429, "rate_limited", "Too many requests");
        return;
    }
    snprintf(key, sizeof key, "%s:%s", route->limit_name, req->ip);
    if (!ms_rl_hit(key, route->limit, route->window))
    {
        ms_res_error(res, 429, "rate_limited", "Too many requests");
        return;
    }
    char cors_origin[sizeof res->allow_origin];
    ms_strlcpy(cors_origin, res->allow_origin, sizeof cors_origin);
    route->handler(req, res);
    // handlers build their own response objects; keep the CORS decision
    ms_strlcpy(res->allow_origin, cors_origin, sizeof res->allow_origin);
}
