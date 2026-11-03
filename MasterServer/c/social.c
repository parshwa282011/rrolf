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

#include "social.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "util.h"

// ------------------------------------------------------------------ Google

int ms_google_enabled()
{
    return MS_CONFIG.google_client_id[0] && MS_CONFIG.google_client_secret[0] &&
           MS_CONFIG.google_redirect_uri[0];
}

int ms_google_authorize_url(char const *state, char *out, size_t size)
{
    if (!ms_google_enabled())
        return -1;
    char redirect[512], e_state[256];
    if (ms_url_encode(redirect, sizeof redirect, MS_CONFIG.google_redirect_uri) <
            0 ||
        ms_url_encode(e_state, sizeof e_state, state) < 0)
        return -1;
    int n = snprintf(out, size,
                     "https://accounts.google.com/o/oauth2/v2/auth?client_id=%s"
                     "&response_type=code&scope=openid%%20email%%20profile"
                     "&redirect_uri=%s&state=%s&prompt=select_account",
                     MS_CONFIG.google_client_id, redirect, e_state);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int ms_google_exchange_code(char const *code, struct ms_discord_user *out)
{
    if (!ms_google_enabled())
        return -1;
    char e_code[512], e_redirect[512], e_id[256], e_secret[256];
    if (ms_url_encode(e_code, sizeof e_code, code) < 0 ||
        ms_url_encode(e_redirect, sizeof e_redirect,
                      MS_CONFIG.google_redirect_uri) < 0 ||
        ms_url_encode(e_id, sizeof e_id, MS_CONFIG.google_client_id) < 0 ||
        ms_url_encode(e_secret, sizeof e_secret,
                      MS_CONFIG.google_client_secret) < 0)
        return -1;
    char form[2048];
    snprintf(form, sizeof form,
             "grant_type=authorization_code&code=%s&redirect_uri=%s"
             "&client_id=%s&client_secret=%s",
             e_code, e_redirect, e_id, e_secret);
    struct curl_slist *headers = curl_slist_append(
        NULL, "Content-Type: application/x-www-form-urlencoded");
    char *response = NULL;
    long status = ms_http_request("POST", "https://oauth2.googleapis.com/token",
                                  headers, form, &response);
    curl_slist_free_all(headers);
    memset(form, 0, sizeof form);
    if (status != 200 || response == NULL)
    {
        ms_log("google", "token exchange failed (http %ld)", status);
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
    char auth[2048];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s", token->valuestring);
    cJSON_Delete(json);
    headers = curl_slist_append(NULL, auth);
    memset(auth, 0, sizeof auth);
    status = ms_http_request("GET", "https://openidconnect.googleapis.com/v1/userinfo",
                             headers, NULL, &response);
    curl_slist_free_all(headers);
    if (status != 200 || response == NULL)
    {
        ms_log("google", "userinfo failed (http %ld)", status);
        free(response);
        return -1;
    }
    json = cJSON_Parse(response);
    free(response);
    cJSON *sub = json ? cJSON_GetObjectItemCaseSensitive(json, "sub") : NULL;
    if (!cJSON_IsString(sub))
    {
        cJSON_Delete(json);
        return -1;
    }
    memset(out, 0, sizeof *out);
    ms_strlcpy(out->id, sub->valuestring, sizeof out->id);
    cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "name");
    if (cJSON_IsString(name))
        ms_strlcpy(out->username, name->valuestring, sizeof out->username);
    // only an address Google has verified is kept
    cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
    cJSON *verified = cJSON_GetObjectItemCaseSensitive(json, "email_verified");
    if (cJSON_IsString(email) && cJSON_IsTrue(verified))
        ms_strlcpy(out->email, email->valuestring, sizeof out->email);
    cJSON_Delete(json);
    return 0;
}

// ------------------------------------------------------------------- Apple

int ms_apple_enabled()
{
    return MS_CONFIG.apple_client_id[0] && MS_CONFIG.apple_team_id[0] &&
           MS_CONFIG.apple_key_id[0] && MS_CONFIG.apple_private_key_file[0] &&
           MS_CONFIG.apple_redirect_uri[0];
}

int ms_apple_authorize_url(char const *state, char *out, size_t size)
{
    if (!ms_apple_enabled())
        return -1;
    char redirect[512], e_state[256];
    if (ms_url_encode(redirect, sizeof redirect, MS_CONFIG.apple_redirect_uri) <
            0 ||
        ms_url_encode(e_state, sizeof e_state, state) < 0)
        return -1;
    // form_post: Apple sends the browser back with a POST, see oauth.c
    int n = snprintf(out, size,
                     "https://appleid.apple.com/auth/authorize?client_id=%s"
                     "&response_type=code&response_mode=form_post"
                     "&scope=name%%20email&redirect_uri=%s&state=%s",
                     MS_CONFIG.apple_client_id, redirect, e_state);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

static void base64url(char *out, size_t size, uint8_t const *in, size_t len)
{
    static char const table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < len && o + 5 < size; i += 3)
    {
        uint32_t v = in[i] << 16 | (i + 1 < len ? in[i + 1] << 8 : 0) |
                     (i + 2 < len ? in[i + 2] : 0);
        out[o++] = table[v >> 18 & 63];
        out[o++] = table[v >> 12 & 63];
        if (i + 1 < len)
            out[o++] = table[v >> 6 & 63];
        if (i + 2 < len)
            out[o++] = table[v & 63];
    }
    out[o] = 0;
}

// returns the length, -1 on bad input
static int base64url_decode(uint8_t *out, size_t size, char const *in,
                            size_t len)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < len; ++i)
    {
        char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '-' || c == '+')
            v = 62;
        else if (c == '_' || c == '/')
            v = 63;
        else if (c == '=')
            break;
        else
            return -1;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (o + 1 >= size)
                return -1;
            out[o++] = (uint8_t)(acc >> bits & 255);
        }
    }
    out[o] = 0;
    return (int)o;
}

// the client secret of Apple is a JWT signed (ES256) with the key of the .p8
// file; it lives five minutes
static int apple_client_secret(char *out, size_t size)
{
    FILE *f = fopen(MS_CONFIG.apple_private_key_file, "r");
    if (f == NULL)
    {
        ms_log("apple", "can't read the private key file");
        return -1;
    }
    EVP_PKEY *key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    if (key == NULL)
    {
        ms_log("apple", "the private key file is not a key");
        return -1;
    }
    cJSON *header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "alg", "ES256");
    cJSON_AddStringToObject(header, "kid", MS_CONFIG.apple_key_id);
    cJSON *claims = cJSON_CreateObject();
    cJSON_AddStringToObject(claims, "iss", MS_CONFIG.apple_team_id);
    cJSON_AddNumberToObject(claims, "iat", (double)time(NULL));
    cJSON_AddNumberToObject(claims, "exp", (double)time(NULL) + 300);
    cJSON_AddStringToObject(claims, "aud", "https://appleid.apple.com");
    cJSON_AddStringToObject(claims, "sub", MS_CONFIG.apple_client_id);
    char *h = cJSON_PrintUnformatted(header), *c = cJSON_PrintUnformatted(claims);
    cJSON_Delete(header);
    cJSON_Delete(claims);
    char h64[256], c64[1024], signing[1400];
    base64url(h64, sizeof h64, (uint8_t *)h, strlen(h));
    base64url(c64, sizeof c64, (uint8_t *)c, strlen(c));
    free(h);
    free(c);
    snprintf(signing, sizeof signing, "%s.%s", h64, c64);

    int result = -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    uint8_t der[128];
    size_t der_len = sizeof der;
    if (ctx != NULL &&
        EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, key) == 1 &&
        EVP_DigestSign(ctx, der, &der_len, (uint8_t *)signing,
                       strlen(signing)) == 1)
    {
        // DER (r, s) to the 64 bytes r || s a JWT wants
        uint8_t const *p = der;
        ECDSA_SIG *sig = d2i_ECDSA_SIG(NULL, &p, (long)der_len);
        if (sig != NULL)
        {
            BIGNUM const *r, *s;
            ECDSA_SIG_get0(sig, &r, &s);
            uint8_t raw[64];
            if (BN_bn2binpad(r, raw, 32) == 32 &&
                BN_bn2binpad(s, raw + 32, 32) == 32)
            {
                char s64[128];
                base64url(s64, sizeof s64, raw, sizeof raw);
                if (snprintf(out, size, "%s.%s", signing, s64) < (int)size)
                    result = 0;
            }
            ECDSA_SIG_free(sig);
        }
    }
    if (result != 0)
        ms_log("apple", "could not sign the client secret");
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return result;
}

int ms_apple_exchange_code(char const *code, struct ms_discord_user *out)
{
    if (!ms_apple_enabled())
        return -1;
    char secret[1024];
    if (apple_client_secret(secret, sizeof secret) != 0)
        return -1;
    char e_code[512], e_redirect[512], e_id[256], e_secret[1500];
    if (ms_url_encode(e_code, sizeof e_code, code) < 0 ||
        ms_url_encode(e_redirect, sizeof e_redirect,
                      MS_CONFIG.apple_redirect_uri) < 0 ||
        ms_url_encode(e_id, sizeof e_id, MS_CONFIG.apple_client_id) < 0 ||
        ms_url_encode(e_secret, sizeof e_secret, secret) < 0)
        return -1;
    char form[3500];
    snprintf(form, sizeof form,
             "grant_type=authorization_code&code=%s&redirect_uri=%s"
             "&client_id=%s&client_secret=%s",
             e_code, e_redirect, e_id, e_secret);
    memset(secret, 0, sizeof secret);
    memset(e_secret, 0, sizeof e_secret);
    struct curl_slist *headers = curl_slist_append(
        NULL, "Content-Type: application/x-www-form-urlencoded");
    char *response = NULL;
    long status = ms_http_request("POST", "https://appleid.apple.com/auth/token",
                                  headers, form, &response);
    curl_slist_free_all(headers);
    memset(form, 0, sizeof form);
    if (status != 200 || response == NULL)
    {
        ms_log("apple", "token exchange failed (http %ld)", status);
        free(response);
        return -1;
    }
    cJSON *json = cJSON_Parse(response);
    free(response);
    cJSON *id_token = json ? cJSON_GetObjectItemCaseSensitive(json, "id_token")
                           : NULL;
    if (!cJSON_IsString(id_token))
    {
        cJSON_Delete(json);
        return -1;
    }
    // header.payload.signature: the payload is the second part
    char const *dot = strchr(id_token->valuestring, '.');
    char const *end = dot ? strchr(dot + 1, '.') : NULL;
    uint8_t payload[2048];
    int len = end ? base64url_decode(payload, sizeof payload, dot + 1,
                                     (size_t)(end - dot - 1))
                  : -1;
    cJSON_Delete(json);
    if (len < 0)
        return -1;
    cJSON *claims = cJSON_Parse((char *)payload);
    cJSON *sub = claims ? cJSON_GetObjectItemCaseSensitive(claims, "sub") : NULL;
    cJSON *aud = claims ? cJSON_GetObjectItemCaseSensitive(claims, "aud") : NULL;
    if (!cJSON_IsString(sub) || !cJSON_IsString(aud) ||
        strcmp(aud->valuestring, MS_CONFIG.apple_client_id) != 0)
    {
        cJSON_Delete(claims);
        return -1;
    }
    memset(out, 0, sizeof *out);
    ms_strlcpy(out->id, sub->valuestring, sizeof out->id);
    cJSON *email = cJSON_GetObjectItemCaseSensitive(claims, "email");
    if (cJSON_IsString(email))
    {
        ms_strlcpy(out->email, email->valuestring, sizeof out->email);
        // the name suggested for a new account: the start of the address
        char *at = strchr(email->valuestring, '@');
        size_t n = at ? (size_t)(at - email->valuestring) : 0;
        if (n > 0 && n < sizeof out->username)
        {
            memcpy(out->username, email->valuestring, n);
            out->username[n] = 0;
        }
    }
    cJSON_Delete(claims);
    return 0;
}
