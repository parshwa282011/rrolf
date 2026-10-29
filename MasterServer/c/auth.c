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

#include <ctype.h>
#include <pthread.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "ratelimit.h"
#include "tmpstore.h"
#include "tokens.h"
#include "util.h"

_Static_assert(crypto_pwhash_STRBYTES <= MS_PW_HASH_SIZE,
               "password hash buffer too small");

int ms_auth_valid_username(char const *s)
{
    if (s == NULL)
        return 0;
    size_t len = strlen(s);
    if (len < 3 || len > MS_USERNAME_MAX)
        return 0;
    if (s[0] == ' ' || s[len - 1] == ' ')
        return 0;
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) && c < 128) && c != '_' && c != '.' && c != '-' &&
            c != ' ')
            return 0;
    }
    return 1;
}

int ms_auth_valid_password(char const *s)
{
    if (s == NULL)
        return 0;
    size_t len = strlen(s);
    return len >= 8 && len <= 128;
}

int ms_auth_valid_email(char const *s)
{
    if (s == NULL)
        return 0;
    size_t len = strlen(s);
    if (len < 3 || len > 254)
        return 0;
    char const *at = strchr(s, '@');
    if (at == NULL || at == s || strchr(at + 1, '@') != NULL)
        return 0;
    char const *dot = strchr(at + 1, '.');
    if (dot == NULL || dot == at + 1 || dot[1] == 0)
        return 0;
    for (size_t i = 0; i < len; ++i)
        if ((unsigned char)s[i] <= ' ' || (unsigned char)s[i] == 127)
            return 0;
    return 1;
}

int ms_auth_hash_password(char out[MS_PW_HASH_SIZE], char const *password)
{
    return crypto_pwhash_str(out, password, strlen(password),
                             crypto_pwhash_OPSLIMIT_INTERACTIVE,
                             crypto_pwhash_MEMLIMIT_INTERACTIVE) == 0
               ? 0
               : -1;
}

// verified for accounts that do not exist so response time does not reveal
// which identifiers are registered
static char dummy_hash[MS_PW_HASH_SIZE];
static pthread_once_t dummy_once = PTHREAD_ONCE_INIT;

static void make_dummy() { ms_auth_hash_password(dummy_hash, "not-a-real-password"); }

static void burn_time(char const *password)
{
    pthread_once(&dummy_once, make_dummy);
    volatile int ignored = crypto_pwhash_str_verify(dummy_hash, password,
                                                    strlen(password));
    (void)ignored;
}

int ms_auth_find_account(char const *identifier, struct ms_db_account *out)
{
    char lower[260];
    ms_str_lower(lower, identifier, sizeof lower);
    if (strchr(lower, '@') != NULL)
        return MS_DB->account_get(MS_DB, ms_db_by_email, lower, NULL, out);
    return MS_DB->account_get(MS_DB, ms_db_by_username, lower, NULL, out);
}

int ms_auth_create_account(struct ms_db_account *a, char const *username,
                           char const *email, char const *password)
{
    memset(a, 0, sizeof *a);
    ms_uuid_generate(a->uuid);
    ms_strlcpy(a->username, username, sizeof a->username);
    ms_strlcpy(a->email, email ? email : "", sizeof a->email);
    a->email_verified = 0; // the email service is not available yet
    if (ms_auth_hash_password(a->pw_hash, password) != 0)
        return MS_DB_ERROR;
    randombytes_buf(a->token_key, sizeof a->token_key);
    a->created_at = ms_now();
    a->last_login_at = a->created_at;
    return MS_DB->account_create(MS_DB, a);
}

enum ms_auth_result ms_auth_verify(char const *uuid, char const *pl_t,
                                   char const *pl_p, struct ms_db_account *out)
{
    if (!ms_uuid_valid(uuid) || pl_p == NULL)
        return ms_auth_invalid;
    int rc = MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, out);
    if (rc == MS_DB_NOT_FOUND)
        return ms_auth_invalid;
    if (rc != MS_DB_OK)
        return ms_auth_error;
    if (pl_t != NULL &&
        !ms_token_check_pl_t(pl_t, out->uuid, out->pw_hash, out->token_key))
        return ms_auth_invalid;
    int64_t issued;
    switch (ms_token_check_pl_p(pl_p, out->uuid, out->username, out->pw_hash,
                                out->token_key, ms_now(),
                                MS_CONFIG.pl_p_lifetime_seconds, &issued))
    {
    case ms_token_ok:
        return ms_auth_ok;
    case ms_token_expired:
        return ms_auth_expired;
    default:
        return ms_auth_invalid;
    }
}

void ms_auth_res_failure(struct ms_res *res, enum ms_auth_result r)
{
    switch (r)
    {
    case ms_auth_expired:
        ms_res_error(res, 401, "expired",
                     "Your session expired, please log in again");
        break;
    case ms_auth_invalid:
        ms_res_error(res, 401, "invalid", "Invalid session");
        break;
    default:
        ms_res_error(res, 500, "server_error", NULL);
        break;
    }
}

cJSON *ms_auth_login_json(struct ms_db_account const *a)
{
    char pl_t[MS_PL_T_SIZE];
    char pl_p[MS_PL_P_SIZE];
    int64_t now = ms_now();
    ms_token_make_pl_t(pl_t, a->uuid, a->pw_hash, a->token_key);
    ms_token_make_pl_p(pl_p, a->uuid, a->username, a->pw_hash, a->token_key,
                       now);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "uuid", a->uuid);
    cJSON_AddStringToObject(o, "username", a->username);
    cJSON_AddBoolToObject(o, "is_dev", a->is_dev);
    cJSON_AddStringToObject(o, "pl_t", pl_t);
    cJSON_AddStringToObject(o, "pl_p", pl_p);
    cJSON_AddNumberToObject(o, "expires_at",
                            (double)(now + MS_CONFIG.pl_p_lifetime_seconds));
    sodium_memzero(pl_t, sizeof pl_t);
    sodium_memzero(pl_p, sizeof pl_p);
    return o;
}

static void conflict_response(struct ms_res *res, int rc)
{
    if (rc == MS_DB_CONFLICT_EMAIL)
        ms_res_error(res, 409, "email_taken", "That email is already in use");
    else if (rc == MS_DB_CONFLICT_USERNAME)
        ms_res_error(res, 409, "username_taken",
                     "That username is already taken");
    else
        ms_res_error(res, 500, "server_error", NULL);
}

void ms_h_signup(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *email = ms_json_str(body, "email");
    char const *username = ms_json_str(body, "username");
    char const *password = ms_json_str(body, "password");
    if (!ms_auth_valid_email(email))
        ms_res_error(res, 400, "invalid_email", "Enter a valid email address");
    else if (!ms_auth_valid_username(username))
        ms_res_error(res, 400, "invalid_username",
                     "Usernames are 3-64 letters, numbers, spaces, . _ -");
    else if (!ms_auth_valid_password(password))
        ms_res_error(res, 400, "invalid_password",
                     "Passwords are 8-128 characters");
    else
    {
        struct ms_db_account a;
        int rc = ms_auth_create_account(&a, username, email, password);
        if (rc == MS_DB_OK)
        {
            ms_log("account", "created %s", a.uuid);
            ms_res_json(res, 201, ms_auth_login_json(&a));
        }
        else
            conflict_response(res, rc);
    }
    cJSON_Delete(body);
}

void ms_h_login(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *identifier = ms_json_str(body, "identifier");
    char const *password = ms_json_str(body, "password");
    if (identifier == NULL || password == NULL || strlen(identifier) > 254 ||
        strlen(password) > 128)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    char lower[260];
    ms_str_lower(lower, identifier, sizeof lower);
    char lock_key[300];
    snprintf(lock_key, sizeof lock_key, "lock:%s", lower);
    if (ms_rl_blocked(lock_key, 5, 900))
    {
        ms_res_error(res, 429, "locked",
                     "Too many failed attempts, try again in 15 minutes");
        cJSON_Delete(body);
        return;
    }

    struct ms_db_account a;
    int rc = ms_auth_find_account(identifier, &a);
    if (rc == MS_DB_ERROR)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        int ok = 0;
        if (rc == MS_DB_OK)
            ok = crypto_pwhash_str_verify(a.pw_hash, password,
                                          strlen(password)) == 0;
        else
            burn_time(password);
        if (!ok)
        {
            ms_rl_add(lock_key, 900);
            ms_res_error(res, 401, "invalid_credentials",
                         "Wrong username or password");
        }
        else
        {
            ms_rl_reset(lock_key);
            a.last_login_at = ms_now();
            MS_DB->account_set_last_login(MS_DB, a.uuid, a.last_login_at);
            ms_res_json(res, 200, ms_auth_login_json(&a));
        }
    }
    cJSON_Delete(body);
}

void ms_h_session(struct ms_req *req, struct ms_res *res)
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
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "uuid", a.uuid);
        cJSON_AddStringToObject(o, "username", a.username);
        cJSON_AddBoolToObject(o, "is_dev", a.is_dev);
        ms_res_json(res, 200, o);
    }
    cJSON_Delete(body);
}

void ms_h_logout_all(struct ms_req *req, struct ms_res *res)
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
    else
    {
        uint8_t key[32];
        randombytes_buf(key, sizeof key);
        if (MS_DB->account_set_credentials(MS_DB, a.uuid, a.pw_hash, key) !=
            MS_DB_OK)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            ms_games_kick_uuid(a.uuid);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            ms_res_json(res, 200, o);
        }
    }
    cJSON_Delete(body);
}

void ms_h_account_info(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r =
        ms_auth_verify(ms_json_str(body, "uuid"), ms_json_str(body, "pl_t"),
                       ms_json_str(body, "pl_p"), &a);
    if (ms_json_str(body, "pl_t") == NULL)
        r = ms_auth_invalid;
    if (r != ms_auth_ok)
    {
        ms_auth_res_failure(res, r);
        cJSON_Delete(body);
        return;
    }
    struct ms_db_link links[8];
    int n = MS_DB->links_get(MS_DB, a.uuid, links, 8);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "uuid", a.uuid);
    cJSON_AddStringToObject(o, "username", a.username);
    cJSON_AddStringToObject(o, "email", a.email);
    cJSON_AddBoolToObject(o, "email_verified", a.email_verified);
    cJSON_AddBoolToObject(o, "is_dev", a.is_dev);
    cJSON *arr = cJSON_AddArrayToObject(o, "links");
    for (int i = 0; i < n; ++i)
    {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "provider", links[i].provider);
        cJSON_AddStringToObject(l, "display_name", links[i].display_name);
        cJSON_AddItemToArray(arr, l);
    }
    ms_res_json(res, 200, o);
    cJSON_Delete(body);
}

void ms_h_complete_profile(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *pending_id = ms_json_str(body, "pending_id");
    char const *username = ms_json_str(body, "username");
    char const *password = ms_json_str(body, "password");
    struct ms_tmp pending;
    if (pending_id == NULL ||
        !ms_tmp_get(ms_tmp_pending, pending_id, &pending))
        ms_res_error(res, 400, "pending_expired",
                     "Sign-in expired, please start again");
    else if (!ms_auth_valid_username(username))
        ms_res_error(res, 400, "invalid_username",
                     "Usernames are 3-64 letters, numbers, spaces, . _ -");
    else if (!ms_auth_valid_password(password))
        ms_res_error(res, 400, "invalid_password",
                     "Passwords are 8-128 characters");
    else
    {
        // the provider account could have been linked elsewhere meanwhile
        struct ms_db_account existing;
        int found = MS_DB->account_get(MS_DB, ms_db_by_link, pending.s1,
                                       pending.s2, &existing);
        if (found == MS_DB_OK)
        {
            ms_res_error(res, 409, "already_linked",
                         "That account is already registered, log in with it");
        }
        else
        {
            // social signups have no email on the account itself
            struct ms_db_account a;
            int rc = ms_auth_create_account(&a, username, "", password);
            if (rc != MS_DB_OK)
                conflict_response(res, rc);
            else
            {
                struct ms_db_link link;
                memset(&link, 0, sizeof link);
                ms_strlcpy(link.provider, pending.s1, sizeof link.provider);
                ms_strlcpy(link.provider_user_id, pending.s2,
                           sizeof link.provider_user_id);
                ms_strlcpy(link.provider_email, pending.s3,
                           sizeof link.provider_email);
                ms_strlcpy(link.display_name, pending.s4,
                           sizeof link.display_name);
                link.linked_at = ms_now();
                MS_DB->link_put(MS_DB, a.uuid, &link);
                ms_tmp_delete(ms_tmp_pending, pending.id);
                ms_log("account", "created %s via %s", a.uuid, link.provider);
                ms_res_json(res, 201, ms_auth_login_json(&a));
            }
        }
    }
    cJSON_Delete(body);
}

void ms_h_provider_unavailable(struct ms_req *req, struct ms_res *res)
{
    ms_res_error(res, 503, "provider_unavailable",
                 "This sign-in method is unavailable: the email service is "
                 "not working right now");
}
