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

// Email verification (at the end of this file) uses the same records and codes:
// an account whose address nobody proved gets a verify_id when it logs in or
// signs up (ms_verify_required), asks for a code to its address
// (/auth/verify/send) and types it back (/auth/verify/confirm), which answers
// with the session. Until then no session exists for it.
//
// Password reset and username change, the same three steps: start (list the ways
// this account can receive a code; "purpose" is "password" or "username") ->
// send (the player picks ONE way, we deliver a 6 digit code; for email they type
// the address) -> confirm (the code + new_password or new_username).
//   * a code lives 10 minutes, has 5 tries, a new one can be asked for after a
//     minute and at most 3 in 10 minutes
//   * Discord: a direct message from the bot. Email: the address typed has to be
//     the account's (or one of its linked ones); the answer is the same whether
//     or not an account has it, so the form can't be used to find accounts

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "discord.h"
#include "email.h"
#include "ratelimit.h"
#include "tmpstore.h"
#include "util.h"

#define OTP_TTL 600
#define OTP_MAX_SENDS 3
#define OTP_MAX_ATTEMPTS 5
#define OTP_MIN_GAP 60

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

static void mask_email(char *out, size_t size, char const *email);

// what a code is for, in the words of the message that carries it
static char const *purpose_label(char const *purpose)
{
    if (strcmp(purpose, "username") == 0)
        return "username change";
    if (strcmp(purpose, "delete") == 0)
        return "ACCOUNT DELETION";
    return "password reset";
}

static char const *purpose_warning(char const *purpose)
{
    if (strcmp(purpose, "delete") == 0)
        return " Someone asked to DELETE your account and everything in it. If that was not "
               "you, do not give the code to anyone and change your password now.";
    return "";
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
    char const *purpose = ms_json_str(body, "purpose");
    ms_strlcpy(reset.s2,
               purpose != NULL && strcmp(purpose, "username") == 0 ? "username"
                                                                   : "password",
               sizeof reset.s2);
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
            // Google and Apple only prove who you are; their address is one
            // the code can be mailed to
            else
                (void)hint;
        }
    }
    (void)has_discord;
    // always shown, so that the list does not tell whether the account exists
    add_method(methods, "email", ms_email_enabled(), NULL,
               ms_email_enabled() ? NULL : "email_unavailable");

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "reset_id", reset.id);
    cJSON_AddItemToObject(o, "methods", methods);
    cJSON_AddStringToObject(o, "purpose", reset.s2);
    ms_res_json(res, 200, o);
    cJSON_Delete(body);
}

// The address typed has to be the account's own or one it linked. Whatever
// happens the answer is the same, so that nobody can look for accounts with it
static void send_by_email(struct ms_tmp *reset, char const *address,
                          struct ms_res *res)
{
    char acct_key[80];
    snprintf(acct_key, sizeof acct_key, "otp:%s", reset->uuid[0] ? reset->uuid : reset->id);
    if (address == NULL || !ms_auth_valid_email(address))
    {
        ms_res_error(res, 400, "bad_email", "Type the email address of the account");
        return;
    }
    if (!ms_email_enabled())
    {
        ms_res_error(res, 503, "provider_unavailable", "Email is not set up");
        return;
    }
    if (reset->n1 >= OTP_MAX_SENDS || !ms_rl_hit(acct_key, OTP_MAX_SENDS, 600))
    {
        ms_res_error(res, 429, "rate_limited",
                     "Too many codes requested, try again later");
        return;
    }
    // checked before the account is looked at, so that it says nothing about it
    if (ms_email_daily_full())
    {
        ms_res_error(res, 429, "daily_limit",
                     "Too many requests today, try again tomorrow");
        return;
    }
    char lower[260], other[260];
    ms_str_lower(lower, address, sizeof lower);
    int match = 0;
    if (reset->uuid[0])
    {
        struct ms_db_account a;
        if (MS_DB->account_get(MS_DB, ms_db_by_uuid, reset->uuid, NULL, &a) ==
            MS_DB_OK)
        {
            ms_str_lower(other, a.email, sizeof other);
            // an address nobody confirmed does not prove who is asking to delete
            match = other[0] && strcmp(other, lower) == 0 &&
                    (a.email_verified || strcmp(reset->s2, "delete") != 0);
            struct ms_db_link links[8];
            int n = MS_DB->links_get(MS_DB, a.uuid, links, 8);
            for (int i = 0; i < n && !match; ++i)
            {
                ms_str_lower(other, links[i].provider_email, sizeof other);
                match = other[0] && strcmp(other, lower) == 0;
            }
        }
    }
    // counts as a code sent in any case
    reset->n1 += 1;
    reset->n3 = ms_now();
    if (match)
    {
        char code[8];
        snprintf(code, sizeof code, "%06u", randombytes_uniform(1000000));
        otp_hash(reset->s1, reset->id, code);
        reset->n2 = 0;
        reset->expires = ms_now() + OTP_TTL;
        char text[500];
        snprintf(text, sizeof text,
                 "Your Parteria %s code is %s. It expires in 10 minutes and "
                 "works once. If you did not ask for it, ignore this email.%s",
                 purpose_label(reset->s2), code, purpose_warning(reset->s2));
        int sent = ms_email_send(address, "Your Parteria code", text, code);
        sodium_memzero(code, sizeof code);
        if (sent != 0)
        {
            reset->s1[0] = 0;
            ms_tmp_update(reset);
            if (sent == -2)
                ms_res_error(res, 429, "daily_limit",
                             "Too many requests today, try again tomorrow");
            else
                ms_res_error(res, 502, "email_failed",
                             "The email could not be sent, try again later");
            sodium_memzero(text, sizeof text);
            return;
        }
        sodium_memzero(text, sizeof text);
    }
    ms_tmp_update(reset);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "method", "email");
    cJSON_AddStringToObject(o, "message",
                            "If an account has that address, a code is on its way");
    cJSON_AddNumberToObject(o, "expires_in", OTP_TTL);
    ms_res_json(res, 200, o);
}

// Deleting an account, step 1 of 3. Needs the session (uuid, PL-T, PL-P: the tokens a
// thief of the page would not have) AND the password, then hands out a reset_id that
// /auth/reset/send turns into a code (email or Discord) and /auth/reset/confirm turns into
// the deletion, when the username is typed back as well.
void ms_h_delete_start(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r =
        ms_auth_verify(ms_json_str(body, "uuid"), ms_json_str(body, "pl_t"),
                       ms_json_str(body, "pl_p"), &a);
    if (ms_json_str(body, "pl_t") == NULL)
        r = ms_auth_invalid;
    char const *password = ms_json_str(body, "password");
    if (r != ms_auth_ok)
    {
        ms_auth_res_failure(res, r);
        cJSON_Delete(body);
        return;
    }
    if (password == NULL || strlen(password) > 128)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    // the same lock as the log in: guessing the password here is guessing it there
    char lower[260], lock_key[300];
    ms_str_lower(lower, a.username, sizeof lower);
    snprintf(lock_key, sizeof lock_key, "lock:%s", lower);
    if (ms_rl_blocked(lock_key, 5, 900))
    {
        ms_res_error(res, 429, "locked", "Too many failed attempts, try again in 15 minutes");
        cJSON_Delete(body);
        return;
    }
    if (crypto_pwhash_str_verify(a.pw_hash, password, strlen(password)) != 0)
    {
        ms_rl_add(lock_key, 900);
        ms_res_error(res, 401, "invalid_credentials", "Wrong password");
        cJSON_Delete(body);
        return;
    }
    ms_rl_reset(lock_key);

    // where a code can go: a confirmed email address, an address a provider vouches for,
    // or Discord. Without any of them nothing proves the owner is the one asking
    struct ms_db_link links[8];
    int n = MS_DB->links_get(MS_DB, a.uuid, links, 8);
    cJSON *hints = cJSON_CreateArray();
    int email_ok = ms_email_enabled() && a.email[0] && a.email_verified;
    char hint[300];
    if (email_ok)
    {
        mask_email(hint, sizeof hint, a.email);
        cJSON_AddItemToArray(hints, cJSON_CreateString(hint));
    }
    int discord = 0;
    for (int i = 0; i < n; ++i)
    {
        if (strcmp(links[i].provider, "discord") == 0 && ms_discord_dm_enabled())
            discord = 1;
        if (ms_email_enabled() && links[i].provider_email[0])
        {
            email_ok = 1;
            mask_email(hint, sizeof hint, links[i].provider_email);
            cJSON_AddItemToArray(hints, cJSON_CreateString(hint));
        }
    }
    if (!email_ok && !discord)
    {
        cJSON_Delete(hints);
        ms_res_error(res, 409, "no_code_channel",
                     "To delete the account we have to send you a code, and there is no "
                     "confirmed email or Discord on it. Add and confirm an email address "
                     "first.");
        cJSON_Delete(body);
        return;
    }
    struct ms_tmp reset;
    memset(&reset, 0, sizeof reset);
    reset.kind = ms_tmp_reset;
    ms_strlcpy(reset.s2, "delete", sizeof reset.s2);
    ms_strlcpy(reset.uuid, a.uuid, sizeof reset.uuid);
    if (ms_tmp_create(&reset, 900) != 0)
    {
        cJSON_Delete(hints);
        ms_res_error(res, 503, "busy", NULL);
        cJSON_Delete(body);
        return;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "reset_id", reset.id);
    cJSON_AddBoolToObject(o, "email", email_ok);
    cJSON_AddBoolToObject(o, "discord", discord);
    cJSON_AddItemToObject(o, "email_hints", hints);
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
        !ms_tmp_get(ms_tmp_reset, reset_id, &reset) ||
        strcmp(reset.s2, "verify") == 0)
    {
        ms_res_error(res, 400, "expired", "Reset expired, please start again");
        cJSON_Delete(body);
        return;
    }
    if (strcmp(method, "discord") != 0 && strcmp(method, "email") != 0)
    {
        ms_res_error(res, 400, "method_unavailable", "Pick Discord or email");
        cJSON_Delete(body);
        return;
    }
    int64_t now = ms_now();
    if (reset.n3 != 0 && now - reset.n3 < OTP_MIN_GAP)
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 0);
        cJSON_AddStringToObject(o, "error", "wait");
        cJSON_AddStringToObject(o, "message", "Wait a minute before asking for another code");
        cJSON_AddNumberToObject(o, "retry_after",
                                (double)(OTP_MIN_GAP - (now - reset.n3)));
        ms_res_json(res, 429, o);
        cJSON_Delete(body);
        return;
    }
    if (strcmp(method, "email") == 0)
    {
        send_by_email(&reset, ms_json_str(body, "email"), res);
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
        reset.n3 = now;
        reset.expires = ms_now() + OTP_TTL;
        ms_tmp_update(&reset);

        char message[500];
        snprintf(message, sizeof message,
                 "Your Parteria %s code is **%s**. It expires in "
                 "10 minutes. If you did not ask for this, ignore it.%s",
                 purpose_label(reset.s2), code, purpose_warning(reset.s2));
        sodium_memzero(code, sizeof code);
        if (ms_discord_send_dm(discord->provider_user_id, message) != 0)
        {
            reset.s1[0] = 0;
            ms_tmp_update(&reset);
            ms_res_error(res, 502, "dm_failed",
                         "Could not send a Discord message. Join the Parteria "
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

// the username typed back to confirm a deletion has to be the account's, letter for letter
static int delete_confirmed(char const *uuid, char const *typed)
{
    struct ms_db_account a;
    return typed != NULL && uuid[0] &&
           MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &a) == MS_DB_OK &&
           strcmp(typed, a.username) == 0;
}

void ms_h_reset_confirm(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *reset_id = ms_json_str(body, "reset_id");
    char const *otp = ms_json_str(body, "otp");
    char const *password = ms_json_str(body, "new_password");
    char const *new_username = ms_json_str(body, "new_username");
    struct ms_tmp reset;
    if (reset_id == NULL || otp == NULL ||
        !ms_tmp_get(ms_tmp_reset, reset_id, &reset) ||
        strcmp(reset.s2, "verify") == 0)
        ms_res_error(res, 400, "expired", "Reset expired, please start again");
    else if (strcmp(reset.s2, "delete") == 0 &&
             !delete_confirmed(reset.uuid, ms_json_str(body, "confirm_username")))
        ms_res_error(res, 400, "confirm_mismatch",
                     "Type your username exactly as it is to confirm");
    else if (strcmp(reset.s2, "delete") != 0 &&
             (strcmp(reset.s2, "username") == 0
                  ? !ms_auth_valid_username(new_username)
                  : !ms_auth_valid_password(password)))
        ms_res_error(res, strcmp(reset.s2, "username") == 0 ? 400 : 400,
                     strcmp(reset.s2, "username") == 0 ? "invalid_username"
                                                       : "invalid_password",
                     strcmp(reset.s2, "username") == 0
                         ? "Usernames are 3-24 letters, digits, space . _ -"
                         : "Passwords are 8-128 characters");
    else if (reset.s1[0] == 0 || reset.uuid[0] == 0)
        ms_res_error(res, 400, "no_code", "Request a code first");
    else if (reset.n2 >= OTP_MAX_ATTEMPTS)
    {
        // five wrong tries: this code is dead, a new one has to be asked for
        reset.s1[0] = 0;
        reset.n2 = 0;
        ms_tmp_update(&reset);
        ms_res_error(res, 429, "too_many_attempts",
                     "Too many wrong codes, ask for a new one");
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
            if (strcmp(reset.s2, "delete") == 0)
            {
                // the code was right, the username typed back: the last point of no return
                ms_tmp_delete(ms_tmp_reset, reset.id);
                if (ms_account_delete_now(reset.uuid) != 0)
                    ms_res_error(res, 500, "server_error", "The account could not be deleted");
                else
                {
                    cJSON *o = cJSON_CreateObject();
                    cJSON_AddBoolToObject(o, "ok", 1);
                    cJSON_AddBoolToObject(o, "deleted", 1);
                    ms_res_json(res, 200, o);
                }
            }
            else if (strcmp(reset.s2, "username") == 0)
            {
                // usernames stay unique, like the uuid
                int rc = MS_DB->account_set_username(MS_DB, reset.uuid,
                                                     new_username);
                if (rc == MS_DB_CONFLICT_USERNAME)
                    ms_res_error(res, 409, "username_taken",
                                 "That username is taken");
                else if (rc != MS_DB_OK)
                    ms_res_error(res, 500, "server_error", NULL);
                else
                {
                    ms_tmp_delete(ms_tmp_reset, reset.id);
                    ms_log("account", "username changed for %s", reset.uuid);
                    cJSON *o = cJSON_CreateObject();
                    cJSON_AddBoolToObject(o, "ok", 1);
                    cJSON_AddStringToObject(o, "username", new_username);
                    ms_res_json(res, 200, o);
                }
            }
            else if (MS_DB->account_get(MS_DB, ms_db_by_uuid, reset.uuid, NULL, &a) !=
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

// ---------------------------------------------------------- email verification

// "someone@example.com" -> "so***@example.com"
static void mask_email(char *out, size_t size, char const *email)
{
    char const *at = strchr(email, '@');
    size_t local = at ? (size_t)(at - email) : strlen(email);
    size_t keep = local < 2 ? local : 2;
    snprintf(out, size, "%.*s***%s", (int)keep, email, at ? at : "");
}

void ms_verify_required(struct ms_res *res, int status,
                        struct ms_db_account const *a)
{
    struct ms_tmp v;
    memset(&v, 0, sizeof v);
    v.kind = ms_tmp_reset;
    ms_strlcpy(v.s2, "verify", sizeof v.s2);
    ms_strlcpy(v.uuid, a->uuid, sizeof v.uuid);
    if (ms_tmp_create(&v, 1800) != 0)
    {
        ms_res_error(res, 503, "busy", NULL);
        return;
    }
    char hint[300];
    mask_email(hint, sizeof hint, a->email);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", status < 300);
    cJSON_AddStringToObject(o, "error", "email_unverified");
    cJSON_AddStringToObject(o, "message", "Verify your email address to continue");
    cJSON_AddStringToObject(o, "verify_id", v.id);
    cJSON_AddStringToObject(o, "email_hint", hint);
    ms_res_json(res, status, o);
}

static int verify_record(cJSON *body, struct ms_tmp *v, struct ms_db_account *a,
                         struct ms_res *res)
{
    char const *id = ms_json_str(body, "verify_id");
    if (id == NULL || !ms_tmp_get(ms_tmp_reset, id, v) ||
        strcmp(v->s2, "verify") != 0)
    {
        ms_res_error(res, 400, "expired", "That took too long, please log in again");
        return 0;
    }
    int rc = MS_DB->account_get(MS_DB, ms_db_by_uuid, v->uuid, NULL, a);
    if (rc != MS_DB_OK)
    {
        ms_res_error(res, rc == MS_DB_NOT_FOUND ? 400 : 500,
                     rc == MS_DB_NOT_FOUND ? "expired" : "server_error", NULL);
        return 0;
    }
    return 1;
}

void ms_h_verify_send(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_tmp v;
    struct ms_db_account a;
    if (!verify_record(body, &v, &a, res))
    {
        cJSON_Delete(body);
        return;
    }
    if (!ms_auth_needs_verification(&a) && a.email_verified)
    {
        // proven in the meantime (the verify_id came from a correct password)
        ms_tmp_delete(ms_tmp_reset, v.id);
        ms_res_json(res, 200, ms_auth_login_json(&a));
        cJSON_Delete(body);
        return;
    }
    int64_t now = ms_now();
    if (v.n3 != 0 && now - v.n3 < OTP_MIN_GAP)
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 0);
        cJSON_AddStringToObject(o, "error", "wait");
        cJSON_AddStringToObject(o, "message",
                                "Wait a minute before asking for another code");
        cJSON_AddNumberToObject(o, "retry_after",
                                (double)(OTP_MIN_GAP - (now - v.n3)));
        ms_res_json(res, 429, o);
        cJSON_Delete(body);
        return;
    }
    // a mistyped address can be corrected here: the player has just proven the
    // password, and the new one has to be proven too
    char const *typed = ms_json_str(body, "email");
    if (typed != NULL && typed[0])
    {
        char lower_new[260], lower_old[260];
        ms_str_lower(lower_new, typed, sizeof lower_new);
        ms_str_lower(lower_old, a.email, sizeof lower_old);
        if (strcmp(lower_new, lower_old) != 0)
        {
            if (!ms_auth_valid_email(typed))
            {
                ms_res_error(res, 400, "invalid_email",
                             "Enter a valid email address");
                cJSON_Delete(body);
                return;
            }
            int rc = MS_DB->account_set_email(MS_DB, a.uuid, typed);
            if (rc == MS_DB_CONFLICT_EMAIL)
            {
                ms_res_error(res, 409, "email_taken",
                             "That email is already in use");
                cJSON_Delete(body);
                return;
            }
            if (rc != MS_DB_OK)
            {
                ms_res_error(res, 500, "server_error", NULL);
                cJSON_Delete(body);
                return;
            }
            ms_strlcpy(a.email, typed, sizeof a.email);
        }
    }
    char acct_key[80];
    snprintf(acct_key, sizeof acct_key, "otp:%s", a.uuid);
    if (ms_email_daily_full())
        ms_res_error(res, 429, "daily_limit",
                     "Too many requests today, try again tomorrow");
    else if (v.n1 >= OTP_MAX_SENDS || !ms_rl_hit(acct_key, OTP_MAX_SENDS, 600))
        ms_res_error(res, 429, "rate_limited",
                     "Too many codes requested, try again later");
    else
    {
        char code[8];
        snprintf(code, sizeof code, "%06u", randombytes_uniform(1000000));
        otp_hash(v.s1, v.id, code);
        v.n1 += 1;
        v.n2 = 0;
        v.n3 = now;
        v.expires = now + OTP_TTL;
        char text[300];
        snprintf(text, sizeof text,
                 "Your Parteria email verification code is %s. It expires in 10 "
                 "minutes and works once. If you did not sign up, ignore this "
                 "email.",
                 code);
        int sent = ms_email_send(a.email, "Verify your Parteria email", text, code);
        sodium_memzero(code, sizeof code);
        sodium_memzero(text, sizeof text);
        if (sent != 0)
        {
            v.s1[0] = 0;
            ms_tmp_update(&v);
            if (sent == -2)
                ms_res_error(res, 429, "daily_limit",
                             "Too many requests today, try again tomorrow");
            else
                ms_res_error(res, 502, "email_failed",
                             "The email could not be sent, try again later");
        }
        else
        {
            ms_tmp_update(&v);
            char hint[300];
            mask_email(hint, sizeof hint, a.email);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            cJSON_AddStringToObject(o, "email_hint", hint);
            cJSON_AddNumberToObject(o, "expires_in", OTP_TTL);
            ms_res_json(res, 200, o);
        }
    }
    cJSON_Delete(body);
}

void ms_h_verify_confirm(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_tmp v;
    struct ms_db_account a;
    char const *otp = ms_json_str(body, "otp");
    if (!verify_record(body, &v, &a, res))
        ;
    else if (otp == NULL)
        ms_res_error(res, 400, "bad_request", NULL);
    else if (v.s1[0] == 0)
        ms_res_error(res, 400, "no_code", "Request a code first");
    else if (v.n2 >= OTP_MAX_ATTEMPTS)
    {
        // five wrong tries: this code is dead, a new one has to be asked for
        v.s1[0] = 0;
        v.n2 = 0;
        ms_tmp_update(&v);
        ms_res_error(res, 429, "too_many_attempts",
                     "Too many wrong codes, ask for a new one");
    }
    else
    {
        v.n2 += 1;
        ms_tmp_update(&v);
        char expected[65], trimmed[16];
        ms_strlcpy(trimmed, otp, sizeof trimmed);
        otp_hash(expected, v.id, trimmed);
        if (!ms_ct_str_equal(expected, v.s1))
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 0);
            cJSON_AddStringToObject(o, "error", "invalid_code");
            cJSON_AddNumberToObject(o, "attempts_left",
                                    OTP_MAX_ATTEMPTS - (double)v.n2);
            ms_res_json(res, 400, o);
        }
        else if (MS_DB->account_set_email_verified(MS_DB, a.uuid) != MS_DB_OK)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            ms_tmp_delete(ms_tmp_reset, v.id);
            a.email_verified = 1;
            a.last_login_at = ms_now();
            MS_DB->account_set_last_login(MS_DB, a.uuid, a.last_login_at);
            ms_log("account", "email verified for %s", a.uuid);
            ms_res_json(res, 200, ms_auth_login_json(&a));
        }
    }
    cJSON_Delete(body);
}
