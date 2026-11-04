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

#include "discord.h"

// Sign in with Google and with Apple, shaped like the Discord functions so that
// oauth.c treats the three alike. `struct ms_discord_user` carries the answer.
// A provider is on only when its ids are set (config.h); nothing secret is ever
// sent to a browser: the authorize urls hold the public client id only.
int ms_google_enabled();
int ms_google_authorize_url(char const *state, char *out, size_t size);
int ms_google_exchange_code(char const *code, struct ms_discord_user *out);

int ms_apple_enabled();
// the refresh token of an Apple sign in, sealed to be kept in the database (and the way
// back), and Apple told that it is no longer wanted. See social.c
int ms_apple_seal(char const *plain, char *out, size_t size);
int ms_apple_revoke(char const *sealed);
// is this JWT really from Apple, and for us? Used by /oauth/apple/notify. The payload
// comes back in *claims on success (cJSON_Delete it)
struct cJSON;
int ms_apple_verify_jwt(char const *jwt, struct cJSON **claims);
int ms_apple_authorize_url(char const *state, char *out, size_t size);
// Apple's id_token comes straight from its token endpoint over TLS, so its
// claims are read without checking the signature (as Apple's docs allow)
int ms_apple_exchange_code(char const *code, struct ms_discord_user *out);
