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

#include "ratelimit.h"

#include <pthread.h>
#include <string.h>

#include "util.h"

#define SLOTS 16384 // power of two
#define PROBES 8

struct slot
{
    char key[96];
    uint32_t count;
    uint32_t window;
    int64_t start;
    int64_t last_seen;
};

static struct slot table[SLOTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t hash_key(char const *s)
{
    uint32_t h = 2166136261u; // FNV-1a
    for (; *s; ++s)
    {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

// returns the slot for `key`, claiming/recycling one if it is not present
static struct slot *find(char const *key, uint32_t window, int64_t now)
{
    uint32_t h = hash_key(key);
    struct slot *victim = NULL;
    for (uint32_t i = 0; i < PROBES; ++i)
    {
        struct slot *s = &table[(h + i) & (SLOTS - 1)];
        if (s->key[0] && strcmp(s->key, key) == 0)
            return s;
        // reusable if empty or its window is long over
        if (s->key[0] == 0 || now - s->start >= (int64_t)s->window)
        {
            if (victim == NULL || victim->key[0] != 0)
                victim = s;
        }
        else if (victim == NULL || (victim->key[0] != 0 &&
                                    s->last_seen < victim->last_seen))
            victim = s;
    }
    memset(victim, 0, sizeof *victim);
    ms_strlcpy(victim->key, key, sizeof victim->key);
    victim->window = window;
    victim->start = now;
    return victim;
}

static void roll(struct slot *s, uint32_t window, int64_t now)
{
    if (now - s->start >= (int64_t)window)
    {
        s->count = 0;
        s->start = now;
    }
    s->window = window;
    s->last_seen = now;
}

int ms_rl_hit(char const *key, uint32_t limit, uint32_t window)
{
    int64_t now = ms_now();
    pthread_mutex_lock(&lock);
    struct slot *s = find(key, window, now);
    roll(s, window, now);
    int allowed = s->count < limit;
    if (s->count < 0xffffffffu)
        ++s->count;
    pthread_mutex_unlock(&lock);
    return allowed;
}

int ms_rl_blocked(char const *key, uint32_t limit, uint32_t window)
{
    int64_t now = ms_now();
    pthread_mutex_lock(&lock);
    struct slot *s = find(key, window, now);
    roll(s, window, now);
    int blocked = s->count >= limit;
    pthread_mutex_unlock(&lock);
    return blocked;
}

void ms_rl_add(char const *key, uint32_t window)
{
    int64_t now = ms_now();
    pthread_mutex_lock(&lock);
    struct slot *s = find(key, window, now);
    roll(s, window, now);
    if (s->count < 0xffffffffu)
        ++s->count;
    pthread_mutex_unlock(&lock);
}

void ms_rl_reset(char const *key)
{
    pthread_mutex_lock(&lock);
    uint32_t h = hash_key(key);
    for (uint32_t i = 0; i < PROBES; ++i)
    {
        struct slot *s = &table[(h + i) & (SLOTS - 1)];
        if (s->key[0] && strcmp(s->key, key) == 0)
        {
            memset(s, 0, sizeof *s);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
}
