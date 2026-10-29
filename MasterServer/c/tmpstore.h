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

#include <stdint.h>

#include "util.h"

// Short lived records that never touch the database: password reset
// challenges, OAuth state, one-time OAuth exchange codes and pending social
// signups. Thread safe, bounded, expired entries are dropped lazily. Lost on
// restart on purpose (everything here lives minutes).

enum ms_tmp_kind
{
    ms_tmp_reset,
    ms_tmp_oauth_state,
    ms_tmp_exchange,
    ms_tmp_pending
};

#define MS_TMP_ID_SIZE 48

struct ms_tmp
{
    enum ms_tmp_kind kind;
    char id[MS_TMP_ID_SIZE];
    char uuid[MS_UUID_SIZE];
    char s1[128];
    char s2[128];
    char s3[256];
    char s4[128];
    int64_t n1;
    int64_t n2;
    int64_t expires;
};

// Fills t->id with a fresh random id, sets expiry to now + ttl and stores a
// copy. Returns 0 on success, -1 if the store is full of live entries.
int ms_tmp_create(struct ms_tmp *t, int64_t ttl_seconds);
// copies the live entry out; returns 1 if found and not expired
int ms_tmp_get(enum ms_tmp_kind kind, char const *id, struct ms_tmp *out);
// writes back changed fields of an entry previously obtained with get
int ms_tmp_update(struct ms_tmp const *t);
void ms_tmp_delete(enum ms_tmp_kind kind, char const *id);
// get + delete in one step (one-time codes)
int ms_tmp_take(enum ms_tmp_kind kind, char const *id, struct ms_tmp *out);
