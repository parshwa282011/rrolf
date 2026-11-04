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
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <pthread.h>
#include <sodium.h>
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
    char refresh[256] = "";
    cJSON *refresh_token = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");
    if (cJSON_IsString(refresh_token))
        ms_strlcpy(refresh, refresh_token->valuestring, sizeof refresh);
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
    ms_strlcpy(out->refresh, refresh, sizeof out->refresh);
    memset(refresh, 0, sizeof refresh);
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

// ------------------------------------------------- Apple: keeping a token to revoke

// The refresh token of an Apple sign in is kept (sealed with libsodium, the key made from
// TOKEN_SECRET_1) for one reason: Apple requires an app to revoke it when the account is
// deleted. hex(nonce || box); "" when it does not fit or sealing failed
int ms_apple_seal(char const *plain, char *out, size_t size)
{
    out[0] = 0;
    size_t len = strlen(plain);
    size_t need = (crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + len) * 2 + 1;
    if (len == 0 || need > size)
        return -1;
    uint8_t key[crypto_secretbox_KEYBYTES];
    crypto_generichash(key, sizeof key, (uint8_t const *)MS_CONFIG.token_secret_1,
                       strlen(MS_CONFIG.token_secret_1),
                       (uint8_t const *)"apple-refresh-token", 19);
    uint8_t box[crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + 256];
    if (len > 256)
        return -1;
    randombytes_buf(box, crypto_secretbox_NONCEBYTES);
    crypto_secretbox_easy(box + crypto_secretbox_NONCEBYTES, (uint8_t const *)plain,
                          len, box, key);
    sodium_memzero(key, sizeof key);
    sodium_bin2hex(out, size, box,
                   crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + len);
    return 0;
}

static int apple_unseal(char const *sealed, char *out, size_t size)
{
    uint8_t box[crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + 256];
    size_t box_len;
    if (sodium_hex2bin(box, sizeof box, sealed, strlen(sealed), NULL, &box_len,
                       NULL) != 0 ||
        box_len <= crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        return -1;
    uint8_t key[crypto_secretbox_KEYBYTES];
    crypto_generichash(key, sizeof key, (uint8_t const *)MS_CONFIG.token_secret_1,
                       strlen(MS_CONFIG.token_secret_1),
                       (uint8_t const *)"apple-refresh-token", 19);
    size_t plain_len = box_len - crypto_secretbox_NONCEBYTES - crypto_secretbox_MACBYTES;
    int rc = -1;
    if (plain_len + 1 <= size &&
        crypto_secretbox_open_easy((uint8_t *)out,
                                   box + crypto_secretbox_NONCEBYTES,
                                   box_len - crypto_secretbox_NONCEBYTES, box,
                                   key) == 0)
    {
        out[plain_len] = 0;
        rc = 0;
    }
    sodium_memzero(key, sizeof key);
    return rc;
}

// tells Apple that the refresh token is no longer wanted (the user's consent to this app
// ends). 0 when Apple accepted it
int ms_apple_revoke(char const *sealed)
{
    if (!ms_apple_enabled() || sealed[0] == 0)
        return -1;
    char token[300], secret[1024];
    if (apple_unseal(sealed, token, sizeof token) != 0 ||
        apple_client_secret(secret, sizeof secret) != 0)
        return -1;
    char e_token[700], e_id[256], e_secret[1500], form[3000];
    if (ms_url_encode(e_token, sizeof e_token, token) < 0 ||
        ms_url_encode(e_id, sizeof e_id, MS_CONFIG.apple_client_id) < 0 ||
        ms_url_encode(e_secret, sizeof e_secret, secret) < 0)
        return -1;
    snprintf(form, sizeof form,
             "client_id=%s&client_secret=%s&token=%s&token_type_hint=refresh_token",
             e_id, e_secret, e_token);
    memset(token, 0, sizeof token);
    memset(secret, 0, sizeof secret);
    memset(e_token, 0, sizeof e_token);
    memset(e_secret, 0, sizeof e_secret);
    struct curl_slist *headers = curl_slist_append(
        NULL, "Content-Type: application/x-www-form-urlencoded");
    char *response = NULL;
    long status = ms_http_request("POST", "https://appleid.apple.com/auth/revoke",
                                  headers, form, &response);
    curl_slist_free_all(headers);
    memset(form, 0, sizeof form);
    free(response);
    if (status != 200)
        ms_log("apple", "revoking a token failed (http %ld)", status);
    return status == 200 ? 0 : -1;
}

// ------------------------------------------------- Apple: server to server notices

// Apple's public keys, kept an hour. The address is a constant on purpose (it is not a
// setting): only the unit test of this file is built with another one
#ifndef MS_APPLE_KEYS_URL
#define MS_APPLE_KEYS_URL "https://appleid.apple.com/auth/keys"
#endif
static pthread_mutex_t keys_lock = PTHREAD_MUTEX_INITIALIZER;
static cJSON *keys_json;
static int64_t keys_at;

static EVP_PKEY *apple_public_key(char const *kid)
{
    pthread_mutex_lock(&keys_lock);
    // a key id we do not know makes us look again, but not more than once a minute
    int known = 0;
    if (keys_json != NULL)
    {
        cJSON const *k;
        cJSON_ArrayForEach(k, cJSON_GetObjectItemCaseSensitive(keys_json, "keys"))
        {
            cJSON const *id = cJSON_GetObjectItemCaseSensitive(k, "kid");
            if (cJSON_IsString(id) && strcmp(id->valuestring, kid) == 0)
                known = 1;
        }
    }
    int64_t now = ms_now();
    if ((keys_json == NULL || now - keys_at > 3600 || (!known && now - keys_at > 60)))
    {
        char *response = NULL;
        long status = ms_http_request("GET", MS_APPLE_KEYS_URL, NULL, NULL, &response);
        cJSON *fresh = status == 200 && response ? cJSON_Parse(response) : NULL;
        free(response);
        if (fresh != NULL)
        {
            cJSON_Delete(keys_json);
            keys_json = fresh;
            keys_at = now;
        }
    }
    EVP_PKEY *pkey = NULL;
    cJSON const *k;
    cJSON const *key_list =
        keys_json ? cJSON_GetObjectItemCaseSensitive(keys_json, "keys") : NULL;
    cJSON_ArrayForEach(k, key_list)
    {
        cJSON const *id = cJSON_GetObjectItemCaseSensitive(k, "kid");
        cJSON const *n = cJSON_GetObjectItemCaseSensitive(k, "n");
        cJSON const *e = cJSON_GetObjectItemCaseSensitive(k, "e");
        cJSON const *kty = cJSON_GetObjectItemCaseSensitive(k, "kty");
        if (!cJSON_IsString(id) || strcmp(id->valuestring, kid) != 0 ||
            !cJSON_IsString(n) || !cJSON_IsString(e) || !cJSON_IsString(kty) ||
            strcmp(kty->valuestring, "RSA") != 0)
            continue;
        uint8_t nb[600], eb[16];
        int nl = base64url_decode(nb, sizeof nb, n->valuestring, strlen(n->valuestring));
        int el = base64url_decode(eb, sizeof eb, e->valuestring, strlen(e->valuestring));
        if (nl <= 0 || el <= 0)
            break;
        BIGNUM *bn = BN_bin2bn(nb, nl, NULL), *be = BN_bin2bn(eb, el, NULL);
        RSA *rsa = RSA_new();
        pkey = EVP_PKEY_new();
        if (bn == NULL || be == NULL || rsa == NULL || pkey == NULL ||
            RSA_set0_key(rsa, bn, be, NULL) != 1)
        {
            BN_free(bn);
            BN_free(be);
            RSA_free(rsa);
            EVP_PKEY_free(pkey);
            pkey = NULL;
        }
        else if (EVP_PKEY_assign_RSA(pkey, rsa) != 1)
        {
            RSA_free(rsa);
            EVP_PKEY_free(pkey);
            pkey = NULL;
        }
        break;
    }
    pthread_mutex_unlock(&keys_lock);
    return pkey;
}

// Checks a JWT that claims to come from Apple: RS256 with one of Apple's keys, issuer
// Apple, addressed to this app (the Services ID or the app's bundle id), not older than two
// days. On success *claims is the payload (free with cJSON_Delete). 0 on success, -1 for
// anything else, which is all the caller learns
int ms_apple_verify_jwt(char const *jwt, cJSON **claims)
{
    *claims = NULL;
    char const *d1 = strchr(jwt, '.');
    char const *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    if (d1 == NULL || d2 == NULL || strchr(d2 + 1, '.') != NULL || strlen(jwt) > 6000)
        return -1;
    uint8_t header_raw[512], payload_raw[4096], sig[600];
    int hl = base64url_decode(header_raw, sizeof header_raw, jwt, (size_t)(d1 - jwt));
    int pl = base64url_decode(payload_raw, sizeof payload_raw, d1 + 1, (size_t)(d2 - d1 - 1));
    int sl = base64url_decode(sig, sizeof sig, d2 + 1, strlen(d2 + 1));
    if (hl <= 0 || pl <= 0 || sl <= 0)
        return -1;
    cJSON *header = cJSON_Parse((char *)header_raw);
    cJSON const *alg = header ? cJSON_GetObjectItemCaseSensitive(header, "alg") : NULL;
    cJSON const *kid = header ? cJSON_GetObjectItemCaseSensitive(header, "kid") : NULL;
    int ok = cJSON_IsString(alg) && strcmp(alg->valuestring, "RS256") == 0 &&
             cJSON_IsString(kid) && strlen(kid->valuestring) < 64;
    EVP_PKEY *pkey = ok ? apple_public_key(kid->valuestring) : NULL;
    cJSON_Delete(header);
    if (pkey == NULL)
        return -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    ok = ctx != NULL &&
         EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
         EVP_DigestVerify(ctx, sig, (size_t)sl, (uint8_t const *)jwt,
                          (size_t)(d2 - jwt)) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    if (!ok)
        return -1;

    cJSON *c = cJSON_Parse((char *)payload_raw);
    cJSON const *iss = c ? cJSON_GetObjectItemCaseSensitive(c, "iss") : NULL;
    cJSON const *aud = c ? cJSON_GetObjectItemCaseSensitive(c, "aud") : NULL;
    cJSON const *iat = c ? cJSON_GetObjectItemCaseSensitive(c, "iat") : NULL;
    int64_t now = (int64_t)time(NULL);
    ok = cJSON_IsString(iss) && strcmp(iss->valuestring, "https://appleid.apple.com") == 0 &&
         cJSON_IsString(aud) &&
         ((MS_CONFIG.apple_client_id[0] &&
           strcmp(aud->valuestring, MS_CONFIG.apple_client_id) == 0) ||
          (MS_CONFIG.apple_app_id[0] &&
           strcmp(aud->valuestring, MS_CONFIG.apple_app_id) == 0)) &&
         cJSON_IsNumber(iat) && (int64_t)iat->valuedouble <= now + 300 &&
         (int64_t)iat->valuedouble >= now - 2 * 24 * 3600;
    if (!ok)
    {
        cJSON_Delete(c);
        return -1;
    }
    *claims = c;
    return 0;
}
