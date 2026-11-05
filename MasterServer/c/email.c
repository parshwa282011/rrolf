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

#include "email.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "discord.h"
#include "util.h"

int ms_email_enabled()
{
    return MS_CONFIG.resend_api_key[0] && MS_CONFIG.resend_event[0];
}

// emails sent today (UTC); it starts again from 0 when the master restarts
static pthread_mutex_t daily_lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t daily_day = -1;
static uint32_t daily_count;

static void daily_roll()
{
    int64_t day = ms_now() / 86400;
    if (day != daily_day)
    {
        daily_day = day;
        daily_count = 0;
    }
}

int ms_email_daily_full()
{
    if (MS_CONFIG.email_daily_limit == 0)
        return 0;
    pthread_mutex_lock(&daily_lock);
    daily_roll();
    int full = daily_count >= MS_CONFIG.email_daily_limit;
    pthread_mutex_unlock(&daily_lock);
    return full;
}

// takes one of today's emails; 0 when there are none left
static int daily_take()
{
    pthread_mutex_lock(&daily_lock);
    daily_roll();
    int ok = MS_CONFIG.email_daily_limit == 0 ||
             daily_count < MS_CONFIG.email_daily_limit;
    if (ok)
        ++daily_count;
    pthread_mutex_unlock(&daily_lock);
    return ok;
}

static void daily_give_back()
{
    pthread_mutex_lock(&daily_lock);
    if (daily_count > 0)
        --daily_count;
    pthread_mutex_unlock(&daily_lock);
}

// Resend's events API: POST /events/send {event, email, payload}. The email itself
// (sender, layout) is the automation Resend runs for that event; it gets the payload
static int send_event(char const *to, char const *subject, char const *text,
                      char const *otp)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", MS_CONFIG.resend_event);
    cJSON_AddStringToObject(root, "email", to);
    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "subject", subject);
    cJSON_AddStringToObject(payload, "text", text);
    cJSON_AddStringToObject(payload, "OTP", otp != NULL ? otp : "");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL)
        return -1;

    char header[320];
    snprintf(header, sizeof header, "Authorization: Bearer %s",
             MS_CONFIG.resend_api_key);
    struct curl_slist *headers = curl_slist_append(NULL, header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    memset(header, 0, sizeof header);
    char *response = NULL;
    long status = ms_http_request("POST", "https://api.resend.com/events/send",
                                  headers, body, &response);
    curl_slist_free_all(headers);
    free(body);
    int ok = status >= 200 && status <= 299;
    if (!ok)
        ms_log("email", "Resend refused it (http %ld): %.400s", status,
               response ? response : "");
    free(response);
    return ok ? 0 : -1;
}

int ms_email_send(char const *to, char const *subject, char const *text,
                  char const *otp)
{
    if (!ms_email_enabled())
        return -1;
    if (!daily_take())
    {
        ms_log("email", "the daily limit of %u emails is used up",
               MS_CONFIG.email_daily_limit);
        return -2;
    }
    int rc = send_event(to, subject, text, otp);
    if (rc != 0)
        daily_give_back();
    return rc;
}

int ms_email_send_admin(char const *subject, char const *text)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int64_t hour = -1;
    static uint32_t count;
    if (MS_CONFIG.admin_email[0] == 0 || !ms_email_enabled())
    {
        ms_log("email", "admin mail not sent (no ADMIN_EMAIL or no email service): %s",
               subject);
        return -1;
    }
    pthread_mutex_lock(&lock);
    int64_t now_hour = ms_now() / 3600;
    if (now_hour != hour)
    {
        hour = now_hour;
        count = 0;
    }
    int allowed = count < 30;
    if (allowed)
        ++count;
    pthread_mutex_unlock(&lock);
    if (!allowed)
        return -1;
    return send_event(MS_CONFIG.admin_email, subject, text, NULL);
}
