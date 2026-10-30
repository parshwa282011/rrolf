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

// Storage interface. Every backend implements `struct ms_db` and is selected
// in ms_db_open() with a switch on MS_CONFIG.db_type, so another engine is one
// new file plus one `case`. All functions are blocking and thread safe (each
// backend owns its connection pool). Sections: accounts, rysteria, gardn,
// leaderboards.

#define MS_DB_OK 0
#define MS_DB_NOT_FOUND 1
#define MS_DB_CONFLICT_USERNAME 2
#define MS_DB_CONFLICT_EMAIL 3
#define MS_DB_CONFLICT_LINK 4
#define MS_DB_ERROR (-1)

#define MS_PW_HASH_SIZE 128
#define MS_GARDN_MAX_SAVES 100
#define MS_NICKNAME_SIZE 32
#define MS_PROVIDER_SIZE 16

struct ms_db_account
{
    char uuid[MS_UUID_SIZE];
    char username[65];
    char email[256]; // "" when the account has none (social signups)
    int email_verified;
    char pw_hash[MS_PW_HASH_SIZE]; // libsodium argon2id string
    uint8_t token_key[32];
    int is_dev;
    int64_t created_at;
    int64_t last_login_at;
};

struct ms_db_link
{
    char provider[MS_PROVIDER_SIZE]; // discord | google | apple
    char provider_user_id[64];
    char provider_email[256];
    char display_name[128];
    int64_t linked_at;
};

// Maps are stored exactly as the old node master did: json objects keyed by
// "id:rarity". Fields are malloc'd json text ("{}" when empty).
struct ms_db_rysteria
{
    double xp;
    // checkpoints of the three saved biomes (hell creek, pvp, garden), one byte
    // each: cp0 | cp1 << 8 | cp2 << 16
    int checkpoint;
    int64_t pvp_points;
    char *petals;
    char *failed_crafts;
    char *mob_kills;
    char *loadout; // json object "slot" -> [id, rarity]
    char nickname[MS_NICKNAME_SIZE];
};

struct ms_db_gsave
{
    char save_id[40];
    uint32_t score;
    char *loadout; // malloc'd json array
    int64_t created_at;
};

struct ms_db_lb_entry
{
    char uuid[MS_UUID_SIZE];
    double score;
    char *extra; // malloc'd json
};

enum ms_db_account_key
{
    ms_db_by_uuid,
    ms_db_by_username, // a = lowercased username
    ms_db_by_email,    // a = lowercased email
    ms_db_by_link      // a = provider, b = provider user id
};

struct ms_db
{
    char const *name;
    void *ctx;

    // MS_DB_OK, MS_DB_CONFLICT_USERNAME, MS_DB_CONFLICT_EMAIL, MS_DB_ERROR
    int (*account_create)(struct ms_db *, struct ms_db_account const *);
    // MS_DB_OK, MS_DB_NOT_FOUND, MS_DB_ERROR
    int (*account_get)(struct ms_db *, enum ms_db_account_key, char const *a,
                       char const *b, struct ms_db_account *out);
    int (*account_set_credentials)(struct ms_db *, char const *uuid,
                                   char const *pw_hash,
                                   uint8_t const token_key[32]);
    int (*account_set_last_login)(struct ms_db *, char const *uuid,
                                  int64_t when);

    // MS_DB_OK, MS_DB_CONFLICT_LINK (provider id belongs to another uuid)
    int (*link_put)(struct ms_db *, char const *uuid,
                    struct ms_db_link const *);
    int (*link_delete)(struct ms_db *, char const *uuid, char const *provider);
    // returns the number of links written to out, or MS_DB_ERROR
    int (*links_get)(struct ms_db *, char const *uuid, struct ms_db_link *out,
                     int max);

    int (*rysteria_get)(struct ms_db *, char const *uuid,
                        struct ms_db_rysteria *out);
    int (*rysteria_put)(struct ms_db *, char const *uuid,
                        struct ms_db_rysteria const *);

    int (*gardn_list)(struct ms_db *, char const *uuid, struct ms_db_gsave *out,
                      int max);
    // idempotent: upserts by save_id, then deletes `remove`, then trims to
    // MS_GARDN_MAX_SAVES (oldest first)
    int (*gardn_apply)(struct ms_db *, char const *uuid,
                       struct ms_db_gsave const *add, int add_count,
                       char const (*remove)[40], int remove_count);

    int (*lb_submit)(struct ms_db *, char const *board, char const *uuid,
                     double score, char const *extra_json);
    int (*lb_top)(struct ms_db *, char const *board, int limit,
                  struct ms_db_lb_entry *out);

    // Change feed for backup servers (GET /backup). `since` / `source` are the
    // cursor and source id from the previous answer (NULL for the first pull).
    // Fills *out with a malloc'd newline-delimited json document: a head line,
    // one line per row change (or per row of a full snapshot) and an end line.
    // NULL when the backend has no feed (MongoDB).
    int (*backup_pull)(struct ms_db *, char const *since, char const *source,
                       char **out, size_t *out_len);

    int (*ping)(struct ms_db *);
    void (*close)(struct ms_db *);
};

// Creates the backend selected by MS_CONFIG.db_type and its schema.
// Returns NULL (after logging) on failure or if the backend was not compiled in.
struct ms_db *ms_db_open();
void ms_db_close(struct ms_db *);

// backend constructors (db_postgres.c / db_mongo.c)
struct ms_db *ms_db_postgres_open();
struct ms_db *ms_db_mongo_open();

// helpers
void ms_db_rysteria_init(struct ms_db_rysteria *);
void ms_db_rysteria_free(struct ms_db_rysteria *);
void ms_db_gsave_free(struct ms_db_gsave *, int count);
void ms_db_lb_free(struct ms_db_lb_entry *, int count);
