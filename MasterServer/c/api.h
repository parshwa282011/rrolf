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

#pragma once

#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>

#include "db.h"
#include "tokens.h"

// Transport independent request/response used by every HTTP handler. Handlers
// run on worker threads, never on the event loop.

struct ms_req
{
    char method[12];
    char path[256];
    char query[512];
    char origin[160];
    char ip[64];
    char server_secret[160];
    char *body; // NUL terminated, may be NULL
    size_t body_len;
};

struct ms_res
{
    int status;
    char *body; // malloc'd
    size_t body_len;
    char content_type[48];
    char location[768]; // when non-empty: Location header
    char allow_origin[160]; // when non-empty: CORS headers are added
};

extern struct ms_db *MS_DB;

// returns 1 if `origin` is one of ALLOWED_ORIGINS
int ms_origin_allowed(char const *origin);

// dispatches to the handler for req->path; applies origin checks, secrets and
// rate limits. Always fills `res`.
void ms_route(struct ms_req *req, struct ms_res *res);
void ms_res_free(struct ms_res *);

// response helpers (take ownership of `obj`)
void ms_res_json(struct ms_res *, int status, cJSON *obj);
void ms_res_error(struct ms_res *, int status, char const *code,
                  char const *message);
void ms_res_redirect(struct ms_res *, char const *url);

// parses the request body; NULL if absent or not a json object
cJSON *ms_req_json(struct ms_req const *);
// string member or NULL
char const *ms_json_str(cJSON const *obj, char const *key);
// query string value, url-decoded into out; returns 1 if present
int ms_query_arg(char const *query, char const *name, char *out, size_t size);

// ---- handlers (auth.c, reset.c, oauth.c, srv.c) ----
typedef void (*ms_handler)(struct ms_req *, struct ms_res *);

void ms_h_signup(struct ms_req *, struct ms_res *);
void ms_h_login(struct ms_req *, struct ms_res *);
void ms_h_session(struct ms_req *, struct ms_res *);
void ms_h_logout_all(struct ms_req *, struct ms_res *);
void ms_h_complete_profile(struct ms_req *, struct ms_res *);

void ms_h_reset_start(struct ms_req *, struct ms_res *);
void ms_h_reset_send(struct ms_req *, struct ms_res *);
void ms_h_reset_confirm(struct ms_req *, struct ms_res *);

void ms_h_discord_start(struct ms_req *, struct ms_res *);
void ms_h_discord_callback(struct ms_req *, struct ms_res *);
void ms_h_discord_link_start(struct ms_req *, struct ms_res *);
void ms_h_oauth_exchange(struct ms_req *, struct ms_res *);
void ms_h_unlink(struct ms_req *, struct ms_res *);
void ms_h_account_info(struct ms_req *, struct ms_res *);
void ms_h_provider_unavailable(struct ms_req *, struct ms_res *);

void ms_h_srv_verify(struct ms_req *, struct ms_res *);
void ms_h_srv_gardn_load(struct ms_req *, struct ms_res *);
void ms_h_srv_gardn_flush(struct ms_req *, struct ms_res *);
void ms_h_srv_lb_submit(struct ms_req *, struct ms_res *);
void ms_h_srv_lb_top(struct ms_req *, struct ms_res *);

void ms_h_health(struct ms_req *, struct ms_res *);

// ---- shared auth helpers (auth.c) ----
enum ms_auth_result
{
    ms_auth_ok,
    ms_auth_invalid,
    ms_auth_expired,
    ms_auth_error
};

// Looks up `uuid` and verifies PL-P (and PL-T when `pl_t` is non-NULL).
enum ms_auth_result ms_auth_verify(char const *uuid, char const *pl_t,
                                   char const *pl_p,
                                   struct ms_db_account *out);
// builds { ok, uuid, username, is_dev, pl_t, pl_p, expires_at }
cJSON *ms_auth_login_json(struct ms_db_account const *);
int ms_auth_hash_password(char out[MS_PW_HASH_SIZE], char const *password);
int ms_auth_valid_username(char const *);
int ms_auth_valid_password(char const *);
int ms_auth_valid_email(char const *);
// find by "name or email"; MS_DB_OK / MS_DB_NOT_FOUND / MS_DB_ERROR
int ms_auth_find_account(char const *identifier, struct ms_db_account *out);
// creates an account; fills a->uuid/token_key/pw_hash; returns MS_DB_* code
int ms_auth_create_account(struct ms_db_account *a, char const *username,
                           char const *email, char const *password);
// Applies an auth failure to a response (401 invalid/expired, 500 error)
void ms_auth_res_failure(struct ms_res *, enum ms_auth_result);

// provided by games.c: disconnect any live game session of this account
void ms_games_kick_uuid(char const *uuid);
