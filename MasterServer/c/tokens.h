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

// PL-T: permanent proof for an account, never expires. It is an HMAC over
// (uuid, hash of the argon2id password string, per-account token_key) so it
// dies when the password changes or token_key is rotated. It never contains
// the password.
//
// PL-P: one week pass. Fixed layout (80 bytes) XORed with a keystream derived
// from (secret 2, hash of the password string, token_key, nonce):
//   [0]      version (1)
//   [1..64]  username, padded with 0x00 ("empty identifier")
//   [65..72] issued-at, unix seconds, little endian
//   [73..79] reserved zero
// token = base64url(nonce[16] || ciphertext[80] || tag[16]); the tag is an
// HMAC over (uuid, nonce, ciphertext) so the timestamp cannot be forged.
// The server can decode the plaintext (it knows the keystream inputs) which
// is how the issue time is read back.

#define MS_PL_T_SIZE 64  // buffer size for a PL-T string
#define MS_PL_P_SIZE 192 // buffer size for a PL-P string
#define MS_USERNAME_MAX 64

enum ms_token_result
{
    ms_token_ok,
    ms_token_malformed,
    ms_token_bad_tag,
    ms_token_username_mismatch,
    ms_token_expired,
    ms_token_from_future
};

void ms_token_make_pl_t(char out[MS_PL_T_SIZE], char const *uuid,
                        char const *pw_hash, uint8_t const token_key[32]);
int ms_token_check_pl_t(char const *token, char const *uuid,
                        char const *pw_hash, uint8_t const token_key[32]);

void ms_token_make_pl_p(char out[MS_PL_P_SIZE], char const *uuid,
                        char const *username, char const *pw_hash,
                        uint8_t const token_key[32], int64_t issued_at);
// On success (and on ms_token_expired) *issued_at is the decoded timestamp.
enum ms_token_result ms_token_check_pl_p(char const *token, char const *uuid,
                                         char const *username,
                                         char const *pw_hash,
                                         uint8_t const token_key[32],
                                         int64_t now, int64_t lifetime,
                                         int64_t *issued_at);
