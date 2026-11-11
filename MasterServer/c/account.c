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

// What the account page (Client/public/account.html) needs besides login:
//   * POST /auth/email/add    an account that signed up with Discord/Google/Apple has no
//                             email; this adds one (it then has to be confirmed by a code)
//   * deleting the account    the steps are in reset.c (/auth/delete/start, then
//                             /auth/reset/send and /auth/reset/confirm); the deletion itself
//                             is ms_account_delete_now() below
//   * POST /oauth/apple/notify  Apple's server to server notices (consent revoked, Apple
//                             account deleted, email forwarding changed)

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "email.h"
#include "games.h"
#include "ratelimit.h"
#include "social.h"
#include "util.h"

// ------------------------------------------------------------------ add an email

void ms_h_email_add(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r =
        ms_auth_verify(ms_json_str(body, "uuid"), ms_json_str(body, "pl_t"),
                       ms_json_str(body, "pl_p"), &a);
    if (ms_json_str(body, "pl_t") == NULL)
        r = ms_auth_invalid;
    char const *email = ms_json_str(body, "email");
    if (r != ms_auth_ok)
        ms_auth_res_failure(res, r);
    else if (a.email[0])
        // changing an address is a different thing (it would let a stolen session take over
        // the password reset): only an account without one can add it here
        ms_res_error(res, 409, "has_email", "This account already has an email address");
    else if (!ms_auth_valid_email(email))
        ms_res_error(res, 400, "invalid_email", "Enter a valid email address");
    else
    {
        int rc = MS_DB->account_set_email(MS_DB, a.uuid, email);
        if (rc == MS_DB_CONFLICT_EMAIL)
            ms_res_error(res, 409, "email_taken", "That email is already in use");
        else if (rc != MS_DB_OK)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            ms_strlcpy(a.email, email, sizeof a.email);
            a.email_verified = 0;
            ms_log("account", "email added to %s", a.uuid);
            if (ms_auth_needs_verification(&a))
                // the code is asked for with /auth/verify/send, like after a sign up
                ms_verify_required(res, 200, &a);
            else
            {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddBoolToObject(o, "ok", 1);
                ms_res_json(res, 200, o);
            }
        }
    }
    cJSON_Delete(body);
}

// ----------------------------------------------------------------- the deletion

// The account is gone when this returns 0: Apple told that its token is no longer wanted,
// the player thrown out of the games, every row of it removed from the database. The
// answer to the owner (the page) comes from the caller.
int ms_account_delete_now(char const *uuid)
{
    struct ms_db_account a;
    if (MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &a) != MS_DB_OK)
        return -1;
    struct ms_db_link links[8];
    int n = MS_DB->links_get(MS_DB, uuid, links, 8);
    char notice_to[260] = "";
    if (a.email[0] && a.email_verified)
        ms_strlcpy(notice_to, a.email, sizeof notice_to);
    for (int i = 0; i < n; ++i)
    {
        if (!notice_to[0] && links[i].provider_email[0])
            ms_strlcpy(notice_to, links[i].provider_email, sizeof notice_to);
        // Apple's rule: the token of a Sign in with Apple is revoked when the account goes
        if (strcmp(links[i].provider, "apple") == 0 && links[i].token[0] &&
            ms_apple_revoke(links[i].token) != 0)
            ms_email_send_admin("Parteria: an Apple token could not be revoked",
                                "An account was deleted but Apple did not accept the "
                                "revocation of its Sign in with Apple token. The player can "
                                "revoke it in Settings > Apple Account > Sign in with Apple.");
    }
    // the games first: whatever they still hold for this player is not written back
    ms_games_drop_uuid(uuid);
    if (MS_DB->account_delete(MS_DB, uuid) != MS_DB_OK)
        return -1;
    ms_log("account", "deleted %s", uuid);
    if (notice_to[0])
    {
        char text[400];
        snprintf(text, sizeof text,
                 "The Parteria account \"%s\" and its progress were deleted at your "
                 "request. If this was not you, write to %s right away.",
                 a.username, MS_CONFIG.admin_email[0] ? MS_CONFIG.admin_email : "the address in our privacy policy");
        ms_email_send(notice_to, "Your Parteria account was deleted", text, NULL);
    }
    return 0;
}

// ---------------------------------------------------------- Apple's notifications

// the ids of the last notices (a notice is processed once even if Apple sends it again)
#define SEEN 512
static char seen[SEEN][64];
static unsigned seen_next;
static pthread_mutex_t seen_lock = PTHREAD_MUTEX_INITIALIZER;

static int already_seen(char const *id)
{
    int found = 0;
    pthread_mutex_lock(&seen_lock);
    for (int i = 0; i < SEEN && !found; ++i)
        found = seen[i][0] && strcmp(seen[i], id) == 0;
    if (!found)
    {
        ms_strlcpy(seen[seen_next % SEEN], id, sizeof seen[0]);
        ++seen_next;
    }
    pthread_mutex_unlock(&seen_lock);
    return found;
}

// Only Apple can get anything out of this: the body is a JWT that has to carry Apple's
// signature (checked against the keys Apple publishes), be addressed to this app and be
// recent. Anything else gets the same bare 401 and changes nothing.
void ms_h_apple_notify(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = req->body_len > 0 && req->body_len < 8000 ? ms_req_json(req) : NULL;
    char const *payload = ms_json_str(body, "payload");
    cJSON *claims = NULL;
    if (payload == NULL || ms_apple_verify_jwt(payload, &claims) != 0)
    {
        ms_log("apple", "a notification was refused (not from Apple, or not for us)");
        ms_res_error(res, 401, "unauthorized", NULL);
        cJSON_Delete(body);
        return;
    }
    cJSON_Delete(body);

    cJSON const *jti = cJSON_GetObjectItemCaseSensitive(claims, "jti");
    cJSON const *events = cJSON_GetObjectItemCaseSensitive(claims, "events");
    // "events" is a JSON document inside a string
    cJSON *ev = cJSON_IsString(events) ? cJSON_Parse(events->valuestring) : NULL;
    char const *type = ev ? ms_json_str(ev, "type") : NULL;
    char const *sub = ev ? ms_json_str(ev, "sub") : NULL;
    if (cJSON_IsString(jti) && already_seen(jti->valuestring))
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        ms_res_json(res, 200, o);
        cJSON_Delete(ev);
        cJSON_Delete(claims);
        return;
    }
    if (type == NULL || sub == NULL || strlen(sub) > 63)
    {
        // signed by Apple but not something we know: say so, change nothing
        ms_log("apple", "a notification from Apple without a type or user");
        ms_email_send_admin("Parteria: an unreadable notice from Apple",
                            "Apple sent a signed notification that has no event type or user.");
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(ev);
        cJSON_Delete(claims);
        return;
    }

    struct ms_db_account a;
    int found = MS_DB->account_get(MS_DB, ms_db_by_link, "apple", sub, &a) == MS_DB_OK;
    int unlinked = 0;
    // the user ended the connection (revoked the app, or deleted their Apple Account): the
    // login with Apple stops working. The game account stays, it has its own password
    if (found && (strcmp(type, "consent-revoked") == 0 || strcmp(type, "account-delete") == 0))
        unlinked = MS_DB->link_delete(MS_DB, a.uuid, "apple") == MS_DB_OK;
    ms_log("apple", "notification %s (%s)", type, found ? "known account" : "no such account");

    char subject[160], text[700];
    snprintf(subject, sizeof subject, "Parteria: Apple notice \"%s\"", type);
    snprintf(text, sizeof text,
             "Apple sent a notice about Sign in with Apple.\n\nEvent: %s\nAccount: %s%s%s\n"
             "Apple's Sign in link removed: %s\n",
             type, found ? a.username : "(none of ours uses that Apple user)",
             found ? " / " : "", found ? a.uuid : "", unlinked ? "yes" : "no");
    // even when today's emails are used up
    ms_email_send_admin(subject, text);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    ms_res_json(res, 200, o);
    cJSON_Delete(ev);
    cJSON_Delete(claims);
}
