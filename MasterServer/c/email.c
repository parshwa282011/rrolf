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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "discord.h"
#include "util.h"

int ms_email_enabled()
{
    return MS_CONFIG.email_api_url[0] && MS_CONFIG.email_api_key[0] &&
           MS_CONFIG.email_from[0];
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

int ms_email_send(char const *to, char const *subject, char const *text)
{
    if (!ms_email_enabled())
        return -1;
    char const *names[4] = {"from", "to", "subject", "text"};
    char const *values[4] = {MS_CONFIG.email_from, to, subject, text};
    char body[4096], next[4096];
    snprintf(body, sizeof body, "%s", MS_CONFIG.email_api_body);
    for (int i = 0; i < 4; ++i)
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
    if (status < 200 || status > 299)
        ms_log("email", "the email service answered http %ld", status);
    free(response);
    return status >= 200 && status <= 299 ? 0 : -1;
}
