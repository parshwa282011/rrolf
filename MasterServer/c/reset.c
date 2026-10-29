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

// Password reset: start (list the ways this account can receive a code) ->
// send (the player picks one, we deliver a 6 digit OTP) -> confirm (OTP +
// new password). Only Discord direct messages can deliver codes right now;
// email, Google and Apple are listed as unavailable.

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "discord.h"
#include "ratelimit.h"
#include "tmpstore.h"
#include "util.h"

#define OTP_TTL 600
#define OTP_MAX_SENDS 3
#define OTP_MAX_ATTEMPTS 5

static void otp_hash(char out[65], char const *reset_id, char const *code)
{
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, (uint8_t const *)MS_CONFIG.otp_pepper,
                                strlen(MS_CONFIG.otp_pepper));
    crypto_auth_hmacsha256_update(&st, (uint8_t const *)reset_id,
                                  strlen(reset_id));
    crypto_auth_hmacsha256_update(&st, (uint8_t const *)"|", 1);
    crypto_auth_hmacsha256_update(&st, (uint8_t const *)code, strlen(code));
    uint8_t mac[32];
    crypto_auth_hmacsha256_final(&st, mac);
    ms_hex_encode(out, mac, sizeof mac);
}

static void add_method(cJSON *arr, char const *method, int available,
                       char const *hint, char const *reason)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "method", method);
    cJSON_AddBoolToObject(m, "available", available);
    if (hint != NULL && hint[0])
        cJSON_AddStringToObject(m, "hint", hint);
    if (reason != NULL)
        cJSON_AddStringToObject(m, "reason", reason);
    cJSON_AddItemToArray(arr, m);
}

// "someone" -> "so***"
static void mask(char *out, size_t size, char const *in)
{
    size_t keep = strlen(in) < 2 ? strlen(in) : 2;
    snprintf(out, size, "%.*s***", (int)keep, in);
}

void ms_h_reset_start(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *identifier = ms_json_str(body, "identifier");
    if (identifier == NULL || identifier[0] == 0 || strlen(identifier) > 254)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    char lower[260];
    ms_str_lower(lower, identifier, sizeof lower);
    char key[300];
    snprintf(key, sizeof key, "resetid:%s", lower);
    if (!ms_rl_hit(key, 5, 3600))
    {
        ms_res_error(res, 429, "rate_limited", "Too many reset requests");
        cJSON_Delete(body);
        return;
    }

    struct ms_db_account a;
    int found = ms_auth_find_account(identifier, &a);
    if (found == MS_DB_ERROR)
    {
        ms_res_error(res, 500, "server_error", NULL);
        cJSON_Delete(body);
        return;
    }
    struct ms_tmp reset;
    memset(&reset, 0, sizeof reset);
    reset.kind = ms_tmp_reset;
    if (found == MS_DB_OK)
        ms_strlcpy(reset.uuid, a.uuid, sizeof reset.uuid);
    if (ms_tmp_create(&reset, 900) != 0)
    {
        ms_res_error(res, 503, "busy", NULL);
        cJSON_Delete(body);
        return;
    }

    cJSON *methods = cJSON_CreateArray();
    int has_discord = 0;
    if (found == MS_DB_OK)
    {
        struct ms_db_link links[8];
        int n = MS_DB->links_get(MS_DB, a.uuid, links, 8);
        char hint[160];
        for (int i = 0; i < n; ++i)
        {
            mask(hint, sizeof hint, links[i].display_name);
            if (strcmp(links[i].provider, "discord") == 0)
            {
                has_discord = 1;
                add_method(methods, "discord", ms_discord_dm_enabled(), hint,
                           ms_discord_dm_enabled() ? NULL
                                                   : "discord_bot_unavailable");
            }
            else
                add_method(methods, links[i].provider, 0, hint,
                           "email_unavailable");
        }
    }
    (void)has_discord;
    // always shown: nothing here can be delivered by email yet
    add_method(methods, "email", 0, NULL, "email_unavailable");

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "reset_id", reset.id);
    cJSON_AddItemToObject(o, "methods", methods);
    cJSON_AddStringToObject(
        o, "notice",
        "Email, Google and Apple are unavailable: the email service is not "
        "working right now.");
    ms_res_json(res, 200, o);
    cJSON_Delete(body);
}

void ms_h_reset_send(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *reset_id = ms_json_str(body, "reset_id");
    char const *method = ms_json_str(body, "method");
    struct ms_tmp reset;
    if (reset_id == NULL || method == NULL ||
        !ms_tmp_get(ms_tmp_reset, reset_id, &reset))
    {
        ms_res_error(res, 400, "expired", "Reset expired, please start again");
        cJSON_Delete(body);
        return;
    }
    if (strcmp(method, "discord") != 0)
    {
        ms_res_error(res, 503, "provider_unavailable",
                     "That method is unavailable: the email service is not "
                     "working right now");
        cJSON_Delete(body);
        return;
    }
    struct ms_db_link links[8];
    int n = reset.uuid[0] ? MS_DB->links_get(MS_DB, reset.uuid, links, 8) : 0;
    struct ms_db_link *discord = NULL;
    for (int i = 0; i < n; ++i)
        if (strcmp(links[i].provider, "discord") == 0)
            discord = &links[i];
    char acct_key[80];
    snprintf(acct_key, sizeof acct_key, "otp:%s", reset.uuid);
    if (discord == NULL || !ms_discord_dm_enabled())
        ms_res_error(res, 400, "method_unavailable",
                     "That method is not available for this account");
    else if (reset.n1 >= OTP_MAX_SENDS || !ms_rl_hit(acct_key, OTP_MAX_SENDS, 600))
        ms_res_error(res, 429, "rate_limited",
                     "Too many codes requested, try again later");
    else
    {
        char code[8];
        snprintf(code, sizeof code, "%06u", randombytes_uniform(1000000));
        otp_hash(reset.s1, reset.id, code);
        reset.n1 += 1;
        reset.n2 = 0;
        reset.expires = ms_now() + OTP_TTL;
        ms_tmp_update(&reset);

        char message[160];
        snprintf(message, sizeof message,
                 "Your Rysteria password reset code is **%s**. It expires in "
                 "10 minutes. If you did not ask for this, ignore it.",
                 code);
        sodium_memzero(code, sizeof code);
        if (ms_discord_send_dm(discord->provider_user_id, message) != 0)
        {
            reset.s1[0] = 0;
            ms_tmp_update(&reset);
            ms_res_error(res, 502, "dm_failed",
                         "Could not send a Discord message. Join the Rysteria "
                         "Discord server and allow direct messages, then try "
                         "again.");
        }
        else
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            cJSON_AddStringToObject(o, "method", "discord");
            cJSON_AddNumberToObject(o, "expires_in", OTP_TTL);
            ms_res_json(res, 200, o);
        }
        sodium_memzero(message, sizeof message);
    }
    cJSON_Delete(body);
}

void ms_h_reset_confirm(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *reset_id = ms_json_str(body, "reset_id");
    char const *otp = ms_json_str(body, "otp");
    char const *password = ms_json_str(body, "new_password");
    struct ms_tmp reset;
    if (reset_id == NULL || otp == NULL ||
        !ms_tmp_get(ms_tmp_reset, reset_id, &reset))
        ms_res_error(res, 400, "expired", "Reset expired, please start again");
    else if (!ms_auth_valid_password(password))
        ms_res_error(res, 400, "invalid_password",
                     "Passwords are 8-128 characters");
    else if (reset.s1[0] == 0 || reset.uuid[0] == 0)
        ms_res_error(res, 400, "no_code", "Request a code first");
    else if (reset.n2 >= OTP_MAX_ATTEMPTS)
    {
        ms_tmp_delete(ms_tmp_reset, reset.id);
        ms_res_error(res, 429, "too_many_attempts",
                     "Too many wrong codes, start again");
    }
    else
    {
        reset.n2 += 1;
        ms_tmp_update(&reset);
        char expected[65];
        char trimmed[16];
        ms_strlcpy(trimmed, otp, sizeof trimmed);
        otp_hash(expected, reset.id, trimmed);
        if (!ms_ct_str_equal(expected, reset.s1))
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 0);
            cJSON_AddStringToObject(o, "error", "invalid_code");
            cJSON_AddNumberToObject(o, "attempts_left",
                                    OTP_MAX_ATTEMPTS - (double)reset.n2);
            ms_res_json(res, 400, o);
        }
        else
        {
            struct ms_db_account a;
            char pw_hash[MS_PW_HASH_SIZE];
            uint8_t key[32];
            randombytes_buf(key, sizeof key);
            if (MS_DB->account_get(MS_DB, ms_db_by_uuid, reset.uuid, NULL, &a) !=
                    MS_DB_OK ||
                ms_auth_hash_password(pw_hash, password) != 0 ||
                MS_DB->account_set_credentials(MS_DB, a.uuid, pw_hash, key) !=
                    MS_DB_OK)
                ms_res_error(res, 500, "server_error", NULL);
            else
            {
                ms_tmp_delete(ms_tmp_reset, reset.id);
                ms_games_kick_uuid(a.uuid);
                char lock_key[300];
                char lower[260];
                ms_str_lower(lower, a.username, sizeof lower);
                snprintf(lock_key, sizeof lock_key, "lock:%s", lower);
                ms_rl_reset(lock_key);
                ms_log("account", "password reset for %s", a.uuid);
                cJSON *o = cJSON_CreateObject();
                cJSON_AddBoolToObject(o, "ok", 1);
                ms_res_json(res, 200, o);
            }
            sodium_memzero(pw_hash, sizeof pw_hash);
        }
    }
    cJSON_Delete(body);
}
