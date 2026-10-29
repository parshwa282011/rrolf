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

#include "discord.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

#define API "https://discord.com/api/v10"
#define MAX_RESPONSE (256 * 1024)

void ms_http_global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void ms_http_global_cleanup() { curl_global_cleanup(); }

int ms_url_encode(char *out, size_t size, char const *in)
{
    static char const hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in; ++in)
    {
        unsigned char c = (unsigned char)*in;
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   c == '.' || c == '~';
        if (o + (safe ? 1 : 3) + 1 > size)
        {
            if (size)
                out[0] = 0;
            return -1;
        }
        if (safe)
            out[o++] = (char)c;
        else
        {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
    return (int)o;
}

struct buffer
{
    char *data;
    size_t len;
};

static size_t on_data(char *chunk, size_t size, size_t n, void *userp)
{
    struct buffer *b = userp;
    size_t add = size * n;
    if (b->len + add > MAX_RESPONSE)
        return 0;
    char *grown = realloc(b->data, b->len + add + 1);
    if (grown == NULL)
        return 0;
    b->data = grown;
    memcpy(b->data + b->len, chunk, add);
    b->len += add;
    b->data[b->len] = 0;
    return add;
}

// Returns the http status (0 on transport failure); *response is malloc'd
static long request(char const *method, char const *url,
                    struct curl_slist *headers, char const *body,
                    char **response)
{
    *response = NULL;
    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return 0;
    struct buffer buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (body != NULL)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "rysteria-master (https://rrolf.io, 1.0)");
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    *response = buf.data;
    return status;
}

int ms_discord_oauth_enabled()
{
    return MS_CONFIG.discord_client_id[0] && MS_CONFIG.discord_client_secret[0] &&
           MS_CONFIG.discord_redirect_uri[0];
}

int ms_discord_dm_enabled() { return MS_CONFIG.discord_bot_token[0] != 0; }

int ms_discord_authorize_url(char const *state, char *out, size_t size)
{
    if (!ms_discord_oauth_enabled())
        return -1;
    char redirect[512];
    char encoded_state[256];
    if (ms_url_encode(redirect, sizeof redirect,
                      MS_CONFIG.discord_redirect_uri) < 0 ||
        ms_url_encode(encoded_state, sizeof encoded_state, state) < 0)
        return -1;
    int n = snprintf(out, size,
                     "https://discord.com/oauth2/authorize?client_id=%s"
                     "&response_type=code&scope=identify%%20email"
                     "&redirect_uri=%s&state=%s&prompt=consent",
                     MS_CONFIG.discord_client_id, redirect, encoded_state);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int ms_discord_exchange_code(char const *code, struct ms_discord_user *out)
{
    if (!ms_discord_oauth_enabled())
        return -1;
    char e_code[512], e_redirect[512], e_id[128], e_secret[256];
    if (ms_url_encode(e_code, sizeof e_code, code) < 0 ||
        ms_url_encode(e_redirect, sizeof e_redirect,
                      MS_CONFIG.discord_redirect_uri) < 0 ||
        ms_url_encode(e_id, sizeof e_id, MS_CONFIG.discord_client_id) < 0 ||
        ms_url_encode(e_secret, sizeof e_secret,
                      MS_CONFIG.discord_client_secret) < 0)
        return -1;
    char form[2048];
    snprintf(form, sizeof form,
             "grant_type=authorization_code&code=%s&redirect_uri=%s"
             "&client_id=%s&client_secret=%s",
             e_code, e_redirect, e_id, e_secret);

    struct curl_slist *headers = curl_slist_append(
        NULL, "Content-Type: application/x-www-form-urlencoded");
    char *response;
    long status = request("POST", API "/oauth2/token", headers, form, &response);
    curl_slist_free_all(headers);
    if (status != 200 || response == NULL)
    {
        ms_log("discord", "token exchange failed (http %ld)", status);
        free(response);
        return -1;
    }
    cJSON *json = cJSON_Parse(response);
    free(response);
    cJSON *token = json ? cJSON_GetObjectItemCaseSensitive(json, "access_token")
                        : NULL;
    if (!cJSON_IsString(token))
    {
        cJSON_Delete(json);
        return -1;
    }
    char auth[1024];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s",
             token->valuestring);
    cJSON_Delete(json);

    headers = curl_slist_append(NULL, auth);
    status = request("GET", API "/users/@me", headers, NULL, &response);
    curl_slist_free_all(headers);
    memset(auth, 0, sizeof auth);
    if (status != 200 || response == NULL)
    {
        ms_log("discord", "users/@me failed (http %ld)", status);
        free(response);
        return -1;
    }
    json = cJSON_Parse(response);
    free(response);
    cJSON *id = json ? cJSON_GetObjectItemCaseSensitive(json, "id") : NULL;
    if (!cJSON_IsString(id))
    {
        cJSON_Delete(json);
        return -1;
    }
    memset(out, 0, sizeof *out);
    ms_strlcpy(out->id, id->valuestring, sizeof out->id);
    cJSON *global = cJSON_GetObjectItemCaseSensitive(json, "global_name");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "username");
    if (cJSON_IsString(global) && global->valuestring[0])
        ms_strlcpy(out->username, global->valuestring, sizeof out->username);
    else if (cJSON_IsString(name))
        ms_strlcpy(out->username, name->valuestring, sizeof out->username);
    cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
    if (cJSON_IsString(email))
        ms_strlcpy(out->email, email->valuestring, sizeof out->email);
    cJSON_Delete(json);
    return 0;
}

int ms_discord_send_dm(char const *user_id, char const *content)
{
    if (!ms_discord_dm_enabled())
        return -1;
    char auth[256];
    snprintf(auth, sizeof auth, "Authorization: Bot %s",
             MS_CONFIG.discord_bot_token);
    struct curl_slist *headers = curl_slist_append(NULL, auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    memset(auth, 0, sizeof auth);

    cJSON *open = cJSON_CreateObject();
    cJSON_AddStringToObject(open, "recipient_id", user_id);
    char *open_body = cJSON_PrintUnformatted(open);
    cJSON_Delete(open);
    char *response;
    long status = request("POST", API "/users/@me/channels", headers, open_body,
                          &response);
    free(open_body);
    if (status != 200 || response == NULL)
    {
        ms_log("discord", "could not open DM channel (http %ld)", status);
        free(response);
        curl_slist_free_all(headers);
        return -1;
    }
    cJSON *channel = cJSON_Parse(response);
    free(response);
    cJSON *channel_id =
        channel ? cJSON_GetObjectItemCaseSensitive(channel, "id") : NULL;
    if (!cJSON_IsString(channel_id))
    {
        cJSON_Delete(channel);
        curl_slist_free_all(headers);
        return -1;
    }
    char url[256];
    snprintf(url, sizeof url, API "/channels/%s/messages",
             channel_id->valuestring);
    cJSON_Delete(channel);

    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "content", content);
    char *message_body = cJSON_PrintUnformatted(message);
    cJSON_Delete(message);
    status = request("POST", url, headers, message_body, &response);
    free(message_body);
    free(response);
    curl_slist_free_all(headers);
    if (status != 200)
        ms_log("discord", "could not send DM (http %ld)", status);
    return status == 200 ? 0 : -1;
}
