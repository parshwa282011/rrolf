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

#pragma once

#include <stddef.h>
#include <stdint.h>

// Logging never receives secrets, passwords, tokens or OTP codes.
void ms_log(char const *type, char const *fmt, ...)
    __attribute__((format(printf, 2, 3)));

int64_t ms_now();    // unix seconds
int64_t ms_now_ms(); // unix milliseconds

// 36 chars + NUL (lowercase v4 uuid)
#define MS_UUID_SIZE 37
void ms_uuid_generate(char out[MS_UUID_SIZE]);
int ms_uuid_valid(char const *uuid);

// out must hold at least 2 * len + 1 bytes
void ms_hex_encode(char *out, uint8_t const *in, size_t len);
// returns number of bytes written, or -1 on malformed input
int ms_hex_decode(uint8_t *out, size_t max, char const *in);

// url-safe, unpadded. out must hold ((len + 2) / 3) * 4 + 1 bytes
size_t ms_b64url_encode(char *out, uint8_t const *in, size_t len);
// returns number of bytes written, or -1 on malformed input / overflow
int ms_b64url_decode(uint8_t *out, size_t max, char const *in);

int ms_ct_equal(void const *a, void const *b, size_t len);
// constant time over two NUL terminated strings (lengths may differ)
int ms_ct_str_equal(char const *a, char const *b);

// copy with truncation, always NUL terminates
void ms_strlcpy(char *dst, char const *src, size_t size);
void ms_str_lower(char *dst, char const *src, size_t size);
