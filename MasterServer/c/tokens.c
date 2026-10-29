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

#include "tokens.h"

#include <sodium.h>
#include <string.h>

#include "config.h"
#include "util.h"

#define PLAIN_SIZE 80
#define NONCE_SIZE 16
#define TAG_SIZE 16
#define RAW_SIZE (NONCE_SIZE + PLAIN_SIZE + TAG_SIZE)
#define PLAIN_VERSION 1

struct hmac
{
    crypto_auth_hmacsha256_state state;
};

static void hmac_begin(struct hmac *h, char const *secret)
{
    crypto_auth_hmacsha256_init(&h->state, (uint8_t const *)secret,
                                strlen(secret));
}

static void hmac_add(struct hmac *h, void const *data, size_t len)
{
    crypto_auth_hmacsha256_update(&h->state, data, len);
}

static void hmac_end(struct hmac *h, uint8_t out[32])
{
    crypto_auth_hmacsha256_final(&h->state, out);
}

// SHA-256 of the argon2id string: the only form of the password that ever
// feeds a token
static void password_binding(uint8_t out[32], char const *pw_hash)
{
    crypto_hash_sha256(out, (uint8_t const *)pw_hash, strlen(pw_hash));
}

static void compute_pl_t(uint8_t out[32], char const *uuid, char const *pw_hash,
                         uint8_t const token_key[32])
{
    uint8_t binding[32];
    password_binding(binding, pw_hash);
    struct hmac h;
    hmac_begin(&h, MS_CONFIG.token_secret_1);
    hmac_add(&h, "PL-T", 4);
    hmac_add(&h, uuid, strlen(uuid));
    hmac_add(&h, binding, sizeof binding);
    hmac_add(&h, token_key, 32);
    hmac_end(&h, out);
}

void ms_token_make_pl_t(char out[MS_PL_T_SIZE], char const *uuid,
                        char const *pw_hash, uint8_t const token_key[32])
{
    uint8_t mac[32];
    compute_pl_t(mac, uuid, pw_hash, token_key);
    ms_b64url_encode(out, mac, sizeof mac);
}

int ms_token_check_pl_t(char const *token, char const *uuid,
                        char const *pw_hash, uint8_t const token_key[32])
{
    if (token == NULL)
        return 0;
    char expected[MS_PL_T_SIZE];
    ms_token_make_pl_t(expected, uuid, pw_hash, token_key);
    return ms_ct_str_equal(token, expected);
}

static void keystream(uint8_t out[PLAIN_SIZE], char const *pw_hash,
                      uint8_t const token_key[32],
                      uint8_t const nonce[NONCE_SIZE])
{
    uint8_t binding[32];
    password_binding(binding, pw_hash);
    for (uint8_t block = 0; block * 32 < PLAIN_SIZE; ++block)
    {
        struct hmac h;
        hmac_begin(&h, MS_CONFIG.token_secret_2);
        hmac_add(&h, "PL-P-KS", 7);
        hmac_add(&h, binding, sizeof binding);
        hmac_add(&h, token_key, 32);
        hmac_add(&h, nonce, NONCE_SIZE);
        hmac_add(&h, &block, 1);
        uint8_t mac[32];
        hmac_end(&h, mac);
        size_t n = PLAIN_SIZE - block * 32;
        memcpy(out + block * 32, mac, n < 32 ? n : 32);
    }
}

static void tag_for(uint8_t out[TAG_SIZE], char const *uuid,
                    char const *pw_hash, uint8_t const token_key[32],
                    uint8_t const nonce[NONCE_SIZE],
                    uint8_t const cipher[PLAIN_SIZE])
{
    uint8_t binding[32];
    password_binding(binding, pw_hash);
    struct hmac h;
    hmac_begin(&h, MS_CONFIG.token_secret_2);
    hmac_add(&h, "PL-P-TAG", 8);
    hmac_add(&h, uuid, strlen(uuid));
    hmac_add(&h, binding, sizeof binding);
    hmac_add(&h, token_key, 32);
    hmac_add(&h, nonce, NONCE_SIZE);
    hmac_add(&h, cipher, PLAIN_SIZE);
    uint8_t mac[32];
    hmac_end(&h, mac);
    memcpy(out, mac, TAG_SIZE);
}

void ms_token_make_pl_p(char out[MS_PL_P_SIZE], char const *uuid,
                        char const *username, char const *pw_hash,
                        uint8_t const token_key[32], int64_t issued_at)
{
    uint8_t raw[RAW_SIZE];
    uint8_t *nonce = raw;
    uint8_t *cipher = raw + NONCE_SIZE;
    uint8_t *tag = raw + NONCE_SIZE + PLAIN_SIZE;
    randombytes_buf(nonce, NONCE_SIZE);

    uint8_t plain[PLAIN_SIZE];
    memset(plain, 0, sizeof plain);
    plain[0] = PLAIN_VERSION;
    size_t name_len = strnlen(username, MS_USERNAME_MAX);
    memcpy(plain + 1, username, name_len); // rest stays 0x00 = empty
    for (int i = 0; i < 8; ++i)
        plain[65 + i] = (uint8_t)((uint64_t)issued_at >> (8 * i));

    uint8_t ks[PLAIN_SIZE];
    keystream(ks, pw_hash, token_key, nonce);
    for (size_t i = 0; i < PLAIN_SIZE; ++i)
        cipher[i] = plain[i] ^ ks[i];
    tag_for(tag, uuid, pw_hash, token_key, nonce, cipher);
    ms_b64url_encode(out, raw, sizeof raw);
}

enum ms_token_result ms_token_check_pl_p(char const *token, char const *uuid,
                                         char const *username,
                                         char const *pw_hash,
                                         uint8_t const token_key[32],
                                         int64_t now, int64_t lifetime,
                                         int64_t *issued_at)
{
    if (issued_at != NULL)
        *issued_at = 0;
    if (token == NULL || strlen(token) > MS_PL_P_SIZE - 1)
        return ms_token_malformed;
    uint8_t raw[RAW_SIZE + 4];
    int n = ms_b64url_decode(raw, sizeof raw, token);
    if (n != RAW_SIZE)
        return ms_token_malformed;
    uint8_t const *nonce = raw;
    uint8_t const *cipher = raw + NONCE_SIZE;
    uint8_t const *tag = raw + NONCE_SIZE + PLAIN_SIZE;

    uint8_t expected[TAG_SIZE];
    tag_for(expected, uuid, pw_hash, token_key, nonce, cipher);
    if (!ms_ct_equal(expected, tag, TAG_SIZE))
        return ms_token_bad_tag;

    uint8_t ks[PLAIN_SIZE];
    keystream(ks, pw_hash, token_key, nonce);
    uint8_t plain[PLAIN_SIZE];
    for (size_t i = 0; i < PLAIN_SIZE; ++i)
        plain[i] = cipher[i] ^ ks[i];
    if (plain[0] != PLAIN_VERSION)
        return ms_token_malformed;

    char name[MS_USERNAME_MAX + 1];
    memcpy(name, plain + 1, MS_USERNAME_MAX);
    name[MS_USERNAME_MAX] = 0;
    if (strcmp(name, username) != 0)
        return ms_token_username_mismatch;

    int64_t at = 0;
    for (int i = 0; i < 8; ++i)
        at |= (int64_t)plain[65 + i] << (8 * i);
    if (issued_at != NULL)
        *issued_at = at;
    if (at > now + 300) // small clock skew allowance
        return ms_token_from_future;
    if (now - at > lifetime)
        return ms_token_expired;
    return ms_token_ok;
}
