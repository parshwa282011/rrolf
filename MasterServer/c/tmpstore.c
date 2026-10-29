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

#include "tmpstore.h"

#include <pthread.h>
#include <sodium.h>
#include <string.h>

#define CAPACITY 4096

static struct ms_tmp entries[CAPACITY];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct ms_tmp *find(enum ms_tmp_kind kind, char const *id, int64_t now)
{
    for (int i = 0; i < CAPACITY; ++i)
    {
        struct ms_tmp *e = &entries[i];
        if (e->id[0] && e->kind == kind && e->expires > now &&
            strcmp(e->id, id) == 0)
            return e;
    }
    return NULL;
}

int ms_tmp_create(struct ms_tmp *t, int64_t ttl)
{
    int64_t now = ms_now();
    pthread_mutex_lock(&lock);
    struct ms_tmp *slot = NULL;
    for (int i = 0; i < CAPACITY; ++i)
        if (entries[i].id[0] == 0 || entries[i].expires <= now)
        {
            slot = &entries[i];
            break;
        }
    if (slot == NULL)
    {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    uint8_t random[24];
    randombytes_buf(random, sizeof random);
    ms_b64url_encode(t->id, random, sizeof random);
    t->expires = now + ttl;
    *slot = *t;
    pthread_mutex_unlock(&lock);
    return 0;
}

int ms_tmp_get(enum ms_tmp_kind kind, char const *id, struct ms_tmp *out)
{
    pthread_mutex_lock(&lock);
    struct ms_tmp *e = find(kind, id, ms_now());
    if (e != NULL)
        *out = *e;
    pthread_mutex_unlock(&lock);
    return e != NULL;
}

int ms_tmp_update(struct ms_tmp const *t)
{
    pthread_mutex_lock(&lock);
    struct ms_tmp *e = find(t->kind, t->id, ms_now());
    if (e != NULL)
        *e = *t;
    pthread_mutex_unlock(&lock);
    return e != NULL;
}

void ms_tmp_delete(enum ms_tmp_kind kind, char const *id)
{
    pthread_mutex_lock(&lock);
    struct ms_tmp *e = find(kind, id, ms_now());
    if (e != NULL)
        memset(e, 0, sizeof *e);
    pthread_mutex_unlock(&lock);
}

int ms_tmp_take(enum ms_tmp_kind kind, char const *id, struct ms_tmp *out)
{
    pthread_mutex_lock(&lock);
    struct ms_tmp *e = find(kind, id, ms_now());
    if (e != NULL)
    {
        *out = *e;
        memset(e, 0, sizeof *e);
    }
    pthread_mutex_unlock(&lock);
    return e != NULL;
}
