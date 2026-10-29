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

#include "util.h"

#include <ctype.h>
#include <pthread.h>
#include <sodium.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void ms_log(char const *type, char const *fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    // log lines must not be forgeable by user supplied text
    for (char *c = message; *c; ++c)
        if ((unsigned char)*c < 0x20 && *c != '\t')
            *c = '?';

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);

    pthread_mutex_lock(&log_lock);
    fprintf(stdout, "[%s::[%s]:\t%s\n", stamp, type, message);
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
}

int64_t ms_now() { return (int64_t)time(NULL); }

int64_t ms_now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void ms_uuid_generate(char out[MS_UUID_SIZE])
{
    uint8_t b[16];
    randombytes_buf(b, sizeof b);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(out, MS_UUID_SIZE,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%"
             "02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
             b[11], b[12], b[13], b[14], b[15]);
}

int ms_uuid_valid(char const *uuid)
{
    if (uuid == NULL || strlen(uuid) != 36)
        return 0;
    for (int i = 0; i < 36; ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (uuid[i] != '-')
                return 0;
        }
        else if (!isxdigit((unsigned char)uuid[i]))
            return 0;
    }
    return 1;
}

void ms_hex_encode(char *out, uint8_t const *in, size_t len)
{
    static char const digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i)
    {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * len] = 0;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int ms_hex_decode(uint8_t *out, size_t max, char const *in)
{
    size_t len = strlen(in);
    if (len % 2 != 0 || len / 2 > max)
        return -1;
    for (size_t i = 0; i < len / 2; ++i)
    {
        int hi = hex_value(in[2 * i]);
        int lo = hex_value(in[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return (int)(len / 2);
}

static char const b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t ms_b64url_encode(char *out, uint8_t const *in, size_t len)
{
    size_t o = 0;
    size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        uint32_t v = in[i] << 16 | in[i + 1] << 8 | in[i + 2];
        out[o++] = b64_alphabet[v >> 18 & 63];
        out[o++] = b64_alphabet[v >> 12 & 63];
        out[o++] = b64_alphabet[v >> 6 & 63];
        out[o++] = b64_alphabet[v & 63];
    }
    if (i + 1 == len)
    {
        uint32_t v = in[i] << 16;
        out[o++] = b64_alphabet[v >> 18 & 63];
        out[o++] = b64_alphabet[v >> 12 & 63];
    }
    else if (i + 2 == len)
    {
        uint32_t v = in[i] << 16 | in[i + 1] << 8;
        out[o++] = b64_alphabet[v >> 18 & 63];
        out[o++] = b64_alphabet[v >> 12 & 63];
        out[o++] = b64_alphabet[v >> 6 & 63];
    }
    out[o] = 0;
    return o;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

int ms_b64url_decode(uint8_t *out, size_t max, char const *in)
{
    size_t len = strlen(in);
    if (len % 4 == 1)
        return -1;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i)
    {
        int v = b64_value(in[i]);
        if (v < 0)
            return -1;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (o >= max)
                return -1;
            out[o++] = (uint8_t)(acc >> bits & 0xff);
        }
    }
    // canonical form only: leftover padding bits must be zero
    if (bits > 0 && (acc & ((1u << bits) - 1)) != 0)
        return -1;
    return (int)o;
}

int ms_ct_equal(void const *a, void const *b, size_t len)
{
    return sodium_memcmp(a, b, len) == 0;
}

int ms_ct_str_equal(char const *a, char const *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    // compare against a fixed amount of work so the length does not leak
    // through timing beyond "different"
    uint8_t diff = la != lb;
    size_t n = la < lb ? la : lb;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i)
        acc |= (uint8_t)(a[i] ^ b[i]);
    return (diff | acc) == 0;
}

void ms_strlcpy(char *dst, char const *src, size_t size)
{
    if (size == 0)
        return;
    size_t n = strnlen(src, size - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

void ms_str_lower(char *dst, char const *src, size_t size)
{
    if (size == 0)
        return;
    size_t i = 0;
    for (; src[i] && i + 1 < size; ++i)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}
