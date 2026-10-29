// Unit tests for the pure parts of the master: encoding, wire format, tokens.

#include <assert.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "proto.h"
#include "tokens.h"
#include "util.h"

static int failures = 0;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void test_encoding()
{
    uint8_t in[] = {0, 1, 2, 250, 251, 252, 253, 254, 255};
    for (size_t len = 0; len <= sizeof in; ++len)
    {
        char b64[64];
        uint8_t out[16];
        ms_b64url_encode(b64, in, len);
        CHECK(ms_b64url_decode(out, sizeof out, b64) == (int)len);
        CHECK(memcmp(in, out, len) == 0);
        char hex[64];
        ms_hex_encode(hex, in, len);
        CHECK(ms_hex_decode(out, sizeof out, hex) == (int)len);
        CHECK(memcmp(in, out, len) == 0);
    }
    uint8_t tiny[2];
    CHECK(ms_b64url_decode(tiny, sizeof tiny, "AAAAAAAA") == -1);
    CHECK(ms_b64url_decode(tiny, sizeof tiny, "A") == -1);
    CHECK(ms_b64url_decode(tiny, sizeof tiny, "AA+/") == -1);

    char uuid[MS_UUID_SIZE];
    ms_uuid_generate(uuid);
    CHECK(ms_uuid_valid(uuid));
    CHECK(uuid[14] == '4');
    CHECK(!ms_uuid_valid("not-a-uuid"));
    CHECK(!ms_uuid_valid(NULL));
}

static void test_proto()
{
    struct ms_writer w;
    ms_writer_init(&w, 256, 1);
    ms_writer_u8(&w, 7);
    ms_writer_string(&w, "hello");
    ms_writer_f64(&w, 1.5e22);
    uint64_t values[] = {0, 1, 127, 128, 300, 70000, 0x7fffffffull,
                         0x80000000ull, 0xffffffffull, 0x123456789abcdefull};
    for (size_t i = 0; i < sizeof values / sizeof *values; ++i)
        ms_writer_varuint(&w, values[i]);
    CHECK(!w.error);
    CHECK(w.data[0] == MS_API_SUCCESS);

    struct ms_reader r;
    ms_reader_init(&r, w.data + 1, w.at - 1);
    CHECK(ms_reader_u8(&r) == 7);
    char s[16];
    ms_reader_string(&r, s, sizeof s);
    CHECK(strcmp(s, "hello") == 0);
    CHECK(ms_reader_f64(&r) == 1.5e22);
    for (size_t i = 0; i < sizeof values / sizeof *values; ++i)
        CHECK(ms_reader_varuint(&r) == values[i]);
    CHECK(!r.error);
    CHECK(ms_reader_remaining(&r) == 0);

    // truncated / hostile input must flag an error, never read out of bounds
    ms_reader_init(&r, (uint8_t const *)"abc", 3);
    ms_reader_string(&r, s, sizeof s);
    CHECK(r.error);
    ms_reader_init(&r, (uint8_t const *)"\x01\x01\x01", 3);
    ms_reader_varuint(&r);
    CHECK(r.error);
    ms_reader_init(&r, (uint8_t const *)"x", 1);
    ms_reader_f64(&r);
    CHECK(r.error);
    uint8_t endless[40];
    memset(endless, 0xff, sizeof endless);
    ms_reader_init(&r, endless, sizeof endless);
    ms_reader_varuint(&r);
    CHECK(r.error);

    // tiny writer overflows safely
    struct ms_writer tiny;
    ms_writer_init(&tiny, 4, 0);
    ms_writer_string(&tiny, "toolong");
    CHECK(tiny.error);
    ms_writer_free(&tiny);
    ms_writer_free(&w);
}

static void test_tokens()
{
    strcpy(MS_CONFIG.token_secret_1, "test-secret-one-0123456789");
    strcpy(MS_CONFIG.token_secret_2, "test-secret-two-0123456789");
    char const *uuid = "11111111-2222-4333-8444-555555555555";
    char const *name = "Player_One";
    char const *pw_hash = "$argon2id$v=19$m=65536,t=2,p=1$abc$def";
    uint8_t key[32];
    randombytes_buf(key, sizeof key);
    int64_t now = 1700000000;
    int64_t week = 7 * 24 * 3600;

    char t[MS_PL_T_SIZE];
    ms_token_make_pl_t(t, uuid, pw_hash, key);
    CHECK(ms_token_check_pl_t(t, uuid, pw_hash, key));
    CHECK(!ms_token_check_pl_t(t, "other", pw_hash, key));
    CHECK(!ms_token_check_pl_t(t, uuid, "$argon2id$changed", key));
    uint8_t key2[32];
    memcpy(key2, key, 32);
    key2[0] ^= 1;
    CHECK(!ms_token_check_pl_t(t, uuid, pw_hash, key2));
    CHECK(!ms_token_check_pl_t("", uuid, pw_hash, key));
    CHECK(!ms_token_check_pl_t(NULL, uuid, pw_hash, key));

    char p[MS_PL_P_SIZE];
    ms_token_make_pl_p(p, uuid, name, pw_hash, key, now);
    int64_t issued = 0;
    CHECK(ms_token_check_pl_p(p, uuid, name, pw_hash, key, now + 10, week,
                              &issued) == ms_token_ok);
    CHECK(issued == now); // the reversible part: timestamp read back
    CHECK(ms_token_check_pl_p(p, uuid, name, pw_hash, key, now + week, week,
                              &issued) == ms_token_ok);
    CHECK(ms_token_check_pl_p(p, uuid, name, pw_hash, key, now + week + 1, week,
                              &issued) == ms_token_expired);
    CHECK(issued == now);
    CHECK(ms_token_check_pl_p(p, uuid, name, pw_hash, key, now - 1000, week,
                              &issued) == ms_token_from_future);
    CHECK(ms_token_check_pl_p(p, uuid, "Player_Two", pw_hash, key, now, week,
                              &issued) == ms_token_username_mismatch);
    CHECK(ms_token_check_pl_p(p, "other-uuid", name, pw_hash, key, now, week,
                              &issued) == ms_token_bad_tag);
    CHECK(ms_token_check_pl_p(p, uuid, name, "$argon2id$changed", key, now,
                              week, &issued) == ms_token_bad_tag);
    CHECK(ms_token_check_pl_p(p, uuid, name, pw_hash, key2, now, week,
                              &issued) == ms_token_bad_tag);
    CHECK(ms_token_check_pl_p("garbage", uuid, name, pw_hash, key, now, week,
                              &issued) == ms_token_malformed);

    // flipping any bit of the token must be rejected (tag covers the body)
    int rejected = 1;
    for (size_t i = 0; p[i]; ++i)
    {
        char saved = p[i];
        p[i] = saved == 'A' ? 'B' : 'A';
        if (ms_token_check_pl_p(p, uuid, name, pw_hash, key, now, week,
                                &issued) == ms_token_ok)
            rejected = 0;
        p[i] = saved;
    }
    CHECK(rejected);

    // a pure-XOR forgery (change the timestamp by xoring the ciphertext) fails
    uint8_t raw[112];
    CHECK(ms_b64url_decode(raw, sizeof raw, p) == 112);
    raw[16 + 65] ^= 0x01;
    char forged[MS_PL_P_SIZE];
    ms_b64url_encode(forged, raw, sizeof raw);
    CHECK(ms_token_check_pl_p(forged, uuid, name, pw_hash, key, now, week,
                              &issued) == ms_token_bad_tag);

    // two tokens for the same input differ (random nonce)
    char p2[MS_PL_P_SIZE];
    ms_token_make_pl_p(p2, uuid, name, pw_hash, key, now);
    CHECK(strcmp(p, p2) != 0);

    // a 64 character username fills the field completely
    char longname[65];
    memset(longname, 'x', 64);
    longname[64] = 0;
    ms_token_make_pl_p(p, uuid, longname, pw_hash, key, now);
    CHECK(ms_token_check_pl_p(p, uuid, longname, pw_hash, key, now, week,
                              &issued) == ms_token_ok);
}

int main()
{
    if (sodium_init() < 0)
        return 2;
    test_encoding();
    test_proto();
    test_tokens();
    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
