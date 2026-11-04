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

static int use_ses() { return strcmp(MS_CONFIG.email_provider, "ses") == 0; }

int ms_email_enabled()
{
    if (use_ses())
        return MS_CONFIG.aws_access_key_id[0] &&
               MS_CONFIG.aws_secret_access_key[0] && MS_CONFIG.aws_region[0] &&
               MS_CONFIG.email_from[0];
    return MS_CONFIG.email_api_url[0] && MS_CONFIG.email_api_key[0] &&
           MS_CONFIG.email_from[0];
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

// replaces every {name} in `in` by `value`; false when `out` is too small
static int replace_all(char *out, size_t size, char const *in,
                       char const *name, char const *value)
{
    char token[32];
    snprintf(token, sizeof token, "{%s}", name);
    size_t tlen = strlen(token), vlen = strlen(value), o = 0;
    while (*in)
    {
        if (strncmp(in, token, tlen) == 0)
        {
            if (o + vlen + 1 > size)
                return 0;
            memcpy(out + o, value, vlen);
            o += vlen;
            in += tlen;
        }
        else
        {
            if (o + 2 > size)
                return 0;
            out[o++] = *in++;
        }
    }
    out[o] = 0;
    return 1;
}

// the value as a JSON string with its quotes
static char *json_string(char const *value)
{
    cJSON *item = cJSON_CreateString(value);
    char *text = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    return text;
}

static int send_generic(char const *to, char const *subject, char const *text)
{
    char const *names[5] = {"from", "from_name", "to", "subject", "text"};
    char const *values[5] = {MS_CONFIG.email_from, MS_CONFIG.email_from_name, to,
                             subject, text};
    char body[4096], next[4096];
    snprintf(body, sizeof body, "%s", MS_CONFIG.email_api_body);
    for (int i = 0; i < 5; ++i)
    {
        char *json = json_string(values[i]);
        int ok = json != NULL && replace_all(next, sizeof next, body, names[i], json);
        free(json);
        if (!ok)
            return -1;
        memcpy(body, next, sizeof body);
    }
    char header[512];
    if (!replace_all(header, sizeof header, MS_CONFIG.email_api_auth, "key",
                     MS_CONFIG.email_api_key))
        return -1;
    struct curl_slist *headers = curl_slist_append(NULL, header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    memset(header, 0, sizeof header);
    char *response = NULL;
    long status = ms_http_request("POST", MS_CONFIG.email_api_url, headers, body,
                                  &response);
    curl_slist_free_all(headers);
    int ok = status >= 200 && status <= 299;
    // some services answer 200 with {"success": false, ...}
    if (ok && response != NULL)
    {
        cJSON *json = cJSON_Parse(response);
        cJSON *success = json ? cJSON_GetObjectItemCaseSensitive(json, "success")
                              : NULL;
        if (cJSON_IsFalse(success))
            ok = 0;
        cJSON_Delete(json);
    }
    if (!ok)
        ms_log("email", "the email service refused it (http %ld): %.200s", status,
               response ? response : "");
    free(response);
    return ok ? 0 : -1;
}

// Amazon SES v2 (SendEmail); libcurl signs the request with the AWS keys
static int send_ses(char const *to, char const *subject, char const *text)
{
    char from[256];
    snprintf(from, sizeof from, "%s <%s>", MS_CONFIG.email_from_name,
             MS_CONFIG.email_from);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "FromEmailAddress", from);
    cJSON *dest = cJSON_AddObjectToObject(root, "Destination");
    cJSON *list = cJSON_AddArrayToObject(dest, "ToAddresses");
    cJSON_AddItemToArray(list, cJSON_CreateString(to));
    cJSON *simple = cJSON_AddObjectToObject(cJSON_AddObjectToObject(root, "Content"),
                                            "Simple");
    cJSON *subj = cJSON_AddObjectToObject(simple, "Subject");
    cJSON_AddStringToObject(subj, "Data", subject);
    cJSON_AddStringToObject(subj, "Charset", "UTF-8");
    cJSON *txt = cJSON_AddObjectToObject(cJSON_AddObjectToObject(simple, "Body"),
                                         "Text");
    cJSON_AddStringToObject(txt, "Data", text);
    cJSON_AddStringToObject(txt, "Charset", "UTF-8");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL)
        return -1;

    char url[160], sigv4[96], userpwd[256];
    char const *region = MS_CONFIG.aws_region;
    snprintf(url, sizeof url,
             "https://email.%s.amazonaws.com/v2/email/outbound-emails", region);
    snprintf(sigv4, sizeof sigv4, "aws:amz:%s:ses", region);
    snprintf(userpwd, sizeof userpwd, "%s:%s", MS_CONFIG.aws_access_key_id,
             MS_CONFIG.aws_secret_access_key);
    struct curl_slist *headers =
        curl_slist_append(NULL, "Content-Type: application/json");
    char *response = NULL;
    long status =
        ms_http_request_sigv4("POST", url, headers, body, &response, sigv4, userpwd);
    curl_slist_free_all(headers);
    memset(userpwd, 0, sizeof userpwd);
    free(body);
    int ok = status >= 200 && status <= 299;
    if (!ok)
        ms_log("email", "SES refused it (http %ld): %.200s", status,
               response ? response : "");
    free(response);
    return ok ? 0 : -1;
}

int ms_email_send(char const *to, char const *subject, char const *text)
{
    if (!ms_email_enabled())
        return -1;
    if (!daily_take())
    {
        ms_log("email", "the daily limit of %u emails is used up",
               MS_CONFIG.email_daily_limit);
        return -2;
    }
    int rc = use_ses() ? send_ses(to, subject, text)
                       : send_generic(to, subject, text);
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
    return use_ses() ? send_ses(MS_CONFIG.admin_email, subject, text)
                     : send_generic(MS_CONFIG.admin_email, subject, text);
}
