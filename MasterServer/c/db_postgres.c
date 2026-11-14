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

#ifdef MS_WITH_POSTGRES

#include <cJSON.h>
#include <ctype.h>
#include <libpq-fe.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "db.h"

// One PostgreSQL schema per section: accounts, rysteria, gardn, leaderboards.

struct pg
{
    PGconn **conns;
    int *busy;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t freed;
};

static PGconn *acquire(struct pg *pg, int *slot)
{
    pthread_mutex_lock(&pg->lock);
    while (1)
    {
        for (int i = 0; i < pg->count; ++i)
        {
            if (pg->busy[i])
                continue;
            pg->busy[i] = 1;
            pthread_mutex_unlock(&pg->lock);
            if (PQstatus(pg->conns[i]) != CONNECTION_OK)
                PQreset(pg->conns[i]);
            *slot = i;
            return pg->conns[i];
        }
        pthread_cond_wait(&pg->freed, &pg->lock);
    }
}

static void release(struct pg *pg, int slot)
{
    pthread_mutex_lock(&pg->lock);
    pg->busy[slot] = 0;
    pthread_cond_signal(&pg->freed);
    pthread_mutex_unlock(&pg->lock);
}

static PGresult *run(PGconn *c, char const *sql, int n,
                     char const *const *params)
{
    return PQexecParams(c, sql, n, NULL, params, NULL, NULL, 0);
}

static int is_command_ok(PGresult *r)
{
    ExecStatusType s = PQresultStatus(r);
    return s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK;
}

static int sqlstate_is(PGresult *r, char const *code)
{
    char const *s = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    return s != NULL && strcmp(s, code) == 0;
}

static int log_failure(char const *what, PGresult *r)
{
    ms_log("db", "postgres %s failed: %s", what,
           PQresultErrorMessage(r) ? PQresultErrorMessage(r) : "?");
    return MS_DB_ERROR;
}

static void fmt_i64(char *out, size_t size, int64_t v)
{
    snprintf(out, size, "%lld", (long long)v);
}

static char const SCHEMA_SQL[] =
    "CREATE SCHEMA IF NOT EXISTS accounts;"
    "CREATE SCHEMA IF NOT EXISTS rysteria;"
    "CREATE SCHEMA IF NOT EXISTS gardn;"
    "CREATE SCHEMA IF NOT EXISTS leaderboards;"
    "CREATE TABLE IF NOT EXISTS accounts.accounts ("
    " uuid text PRIMARY KEY,"
    " username text NOT NULL,"
    " username_lower text NOT NULL,"
    " email text NOT NULL DEFAULT '',"
    " email_lower text NOT NULL DEFAULT '',"
    " email_verified boolean NOT NULL DEFAULT false,"
    " pw_hash text NOT NULL,"
    " token_key text NOT NULL,"
    " is_dev boolean NOT NULL DEFAULT false,"
    " created_at bigint NOT NULL,"
    " last_login_at bigint NOT NULL DEFAULT 0);"
    // Added later: the rows that were there get '' (no tag), nothing else changes. It may be NULL
    // (read as no tag) on purpose: a copy of the table made as jsonb (scripts/common/remote-copy.py)
    // from a master from before the tags has no "tag" in it, which fills the
    // column with NULL; a NOT NULL column would refuse that row
    "ALTER TABLE accounts.accounts ADD COLUMN IF NOT EXISTS tag text DEFAULT '';"
    "CREATE UNIQUE INDEX IF NOT EXISTS accounts_username_lower_idx"
    " ON accounts.accounts (username_lower);"
    "CREATE UNIQUE INDEX IF NOT EXISTS accounts_email_lower_idx"
    " ON accounts.accounts (email_lower) WHERE email_lower <> '';"
    "CREATE TABLE IF NOT EXISTS accounts.links ("
    " uuid text NOT NULL REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " provider text NOT NULL,"
    " provider_user_id text NOT NULL,"
    " provider_email text NOT NULL DEFAULT '',"
    " display_name text NOT NULL DEFAULT '',"
    " linked_at bigint NOT NULL,"
    " PRIMARY KEY (uuid, provider),"
    " CONSTRAINT links_provider_user_unique UNIQUE (provider, provider_user_id));"
    "ALTER TABLE accounts.links ADD COLUMN IF NOT EXISTS provider_token text NOT NULL DEFAULT '';"
    "CREATE TABLE IF NOT EXISTS rysteria.players ("
    " uuid text PRIMARY KEY REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " xp double precision NOT NULL DEFAULT 0,"
    " checkpoint integer NOT NULL DEFAULT 0,"
    " pvp_points bigint NOT NULL DEFAULT 0,"
    " petals jsonb NOT NULL DEFAULT '{}',"
    " failed_crafts jsonb NOT NULL DEFAULT '{}',"
    " mob_kills jsonb NOT NULL DEFAULT '{}',"
    " loadout jsonb NOT NULL DEFAULT '{}',"
    " nickname text NOT NULL DEFAULT '',"
    " updated_at bigint NOT NULL DEFAULT 0);"
    "ALTER TABLE rysteria.players ALTER COLUMN checkpoint TYPE integer;"
    "ALTER TABLE rysteria.players ADD COLUMN IF NOT EXISTS pvp_points bigint NOT NULL DEFAULT 0;"
    "CREATE TABLE IF NOT EXISTS gardn.saves ("
    " uuid text NOT NULL REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " save_id text NOT NULL,"
    " score bigint NOT NULL,"
    " loadout jsonb NOT NULL,"
    " created_at bigint NOT NULL,"
    " PRIMARY KEY (uuid, save_id));"
    "CREATE TABLE IF NOT EXISTS gardn.crash_backups ("
    " uuid text PRIMARY KEY REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " score bigint NOT NULL,"
    " loadout jsonb NOT NULL,"
    " updated_at bigint NOT NULL);"
    "CREATE TABLE IF NOT EXISTS leaderboards.entries ("
    " board text NOT NULL,"
    " uuid text NOT NULL REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " score double precision NOT NULL,"
    " extra jsonb NOT NULL DEFAULT '{}',"
    " updated_at bigint NOT NULL,"
    " PRIMARY KEY (board, uuid));"
    "CREATE INDEX IF NOT EXISTS leaderboards_entries_score_idx"
    " ON leaderboards.entries (board, score DESC);"
    // the change log of syncd (gone): its triggers and the table that grew with every write
    "DROP SCHEMA IF EXISTS sync CASCADE;";

static void read_account(PGresult *r, struct ms_db_account *out)
{
    memset(out, 0, sizeof *out);
    ms_strlcpy(out->uuid, PQgetvalue(r, 0, 0), sizeof out->uuid);
    ms_strlcpy(out->username, PQgetvalue(r, 0, 1), sizeof out->username);
    ms_strlcpy(out->email, PQgetvalue(r, 0, 2), sizeof out->email);
    out->email_verified = PQgetvalue(r, 0, 3)[0] == 't';
    ms_strlcpy(out->pw_hash, PQgetvalue(r, 0, 4), sizeof out->pw_hash);
    ms_hex_decode(out->token_key, sizeof out->token_key, PQgetvalue(r, 0, 5));
    out->is_dev = PQgetvalue(r, 0, 6)[0] == 't';
    out->created_at = atoll(PQgetvalue(r, 0, 7));
    out->last_login_at = atoll(PQgetvalue(r, 0, 8));
    ms_strlcpy(out->tag, PQgetvalue(r, 0, 9), sizeof out->tag);
}

static int pg_account_create(struct ms_db *db, struct ms_db_account const *a)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char lower_name[sizeof a->username];
    char lower_email[sizeof a->email];
    ms_str_lower(lower_name, a->username, sizeof lower_name);
    ms_str_lower(lower_email, a->email, sizeof lower_email);
    char key[65];
    ms_hex_encode(key, a->token_key, 32);
    char created[32];
    fmt_i64(created, sizeof created, a->created_at);
    char const *params[] = {a->uuid,
                            a->username,
                            lower_name,
                            a->email,
                            lower_email,
                            a->email_verified ? "true" : "false",
                            a->pw_hash,
                            key,
                            a->is_dev ? "true" : "false",
                            created};
    PGresult *r = run(c,
                      "INSERT INTO accounts.accounts (uuid, username,"
                      " username_lower, email, email_lower, email_verified,"
                      " pw_hash, token_key, is_dev, created_at)"
                      " VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
                      10, params);
    int result;
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        result = MS_DB_OK;
    else if (sqlstate_is(r, "23505"))
    {
        char const *constraint = PQresultErrorField(r, PG_DIAG_CONSTRAINT_NAME);
        result = constraint != NULL && strstr(constraint, "email")
                     ? MS_DB_CONFLICT_EMAIL
                     : MS_DB_CONFLICT_USERNAME;
    }
    else
        result = log_failure("account_create", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_account_get(struct ms_db *db, enum ms_db_account_key key,
                          char const *a, char const *b,
                          struct ms_db_account *out)
{
    struct pg *pg = db->ctx;
    static char const columns[] =
        "SELECT a.uuid, a.username, a.email, a.email_verified, a.pw_hash,"
        " a.token_key, a.is_dev, a.created_at, a.last_login_at,"
        " COALESCE(a.tag, '')"
        " FROM accounts.accounts a";
    char sql[512];
    char const *params[2] = {a, b};
    int n = 1;
    switch (key)
    {
    case ms_db_by_uuid:
        snprintf(sql, sizeof sql, "%s WHERE a.uuid = $1", columns);
        break;
    case ms_db_by_username:
        snprintf(sql, sizeof sql, "%s WHERE a.username_lower = $1", columns);
        break;
    case ms_db_by_email:
        snprintf(sql, sizeof sql,
                 "%s WHERE a.email_lower = $1 AND a.email_lower <> ''",
                 columns);
        break;
    case ms_db_by_link:
        snprintf(sql, sizeof sql,
                 "%s JOIN accounts.links l ON l.uuid = a.uuid"
                 " WHERE l.provider = $1 AND l.provider_user_id = $2",
                 columns);
        n = 2;
        break;
    default:
        return MS_DB_ERROR;
    }
    int slot;
    PGconn *c = acquire(pg, &slot);
    PGresult *r = run(c, sql, n, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("account_get", r);
    else if (PQntuples(r) == 0)
        result = MS_DB_NOT_FOUND;
    else
    {
        read_account(r, out);
        result = MS_DB_OK;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_simple(struct ms_db *db, char const *what, char const *sql,
                     int n, char const *const *params)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    PGresult *r = run(c, sql, n, params);
    int result = is_command_ok(r) ? MS_DB_OK : log_failure(what, r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_account_set_credentials(struct ms_db *db, char const *uuid,
                                      char const *pw_hash,
                                      uint8_t const token_key[32])
{
    char key[65];
    ms_hex_encode(key, token_key, 32);
    char const *params[] = {uuid, pw_hash, key};
    return pg_simple(db, "set_credentials",
                     "UPDATE accounts.accounts SET pw_hash = $2,"
                     " token_key = $3 WHERE uuid = $1",
                     3, params);
}

static int pg_account_set_last_login(struct ms_db *db, char const *uuid,
                                     int64_t when)
{
    char ts[32];
    fmt_i64(ts, sizeof ts, when);
    char const *params[] = {uuid, ts};
    return pg_simple(db, "set_last_login",
                     "UPDATE accounts.accounts SET last_login_at = $2"
                     " WHERE uuid = $1",
                     2, params);
}

static int pg_account_set_email_verified(struct ms_db *db, char const *uuid)
{
    char const *params[] = {uuid};
    return pg_simple(db, "set_email_verified",
                     "UPDATE accounts.accounts SET email_verified = true"
                     " WHERE uuid = $1",
                     1, params);
}

static int pg_account_set_email(struct ms_db *db, char const *uuid,
                                char const *email)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char lower[sizeof((struct ms_db_account *)0)->email];
    ms_str_lower(lower, email, sizeof lower);
    char const *params[] = {uuid, email, lower};
    PGresult *r = run(c,
                      "UPDATE accounts.accounts SET email = $2,"
                      " email_lower = $3, email_verified = false"
                      " WHERE uuid = $1",
                      3, params);
    int result;
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        result = MS_DB_OK;
    else if (sqlstate_is(r, "23505"))
        result = MS_DB_CONFLICT_EMAIL;
    else
        result = log_failure("account_set_email", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_account_set_username(struct ms_db *db, char const *uuid,
                                   char const *username)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char lower[sizeof((struct ms_db_account *)0)->username];
    ms_str_lower(lower, username, sizeof lower);
    char const *params[] = {uuid, username, lower};
    PGresult *r = run(c,
                      "UPDATE accounts.accounts SET username = $2,"
                      " username_lower = $3 WHERE uuid = $1",
                      3, params);
    int result;
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        result = MS_DB_OK;
    else if (sqlstate_is(r, "23505"))
        result = MS_DB_CONFLICT_USERNAME;
    else
        result = log_failure("account_set_username", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_link_put(struct ms_db *db, char const *uuid,
                       struct ms_db_link const *l)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char linked[32];
    fmt_i64(linked, sizeof linked, l->linked_at);
    char const *params[] = {uuid,           l->provider,     l->provider_user_id,
                            l->provider_email, l->display_name, linked,
                            l->token};
    PGresult *r = run(c,
                      "INSERT INTO accounts.links (uuid, provider,"
                      " provider_user_id, provider_email, display_name,"
                      " linked_at, provider_token) VALUES ($1,$2,$3,$4,$5,$6,$7)"
                      " ON CONFLICT (uuid, provider) DO UPDATE SET"
                      " provider_user_id = EXCLUDED.provider_user_id,"
                      " provider_email = EXCLUDED.provider_email,"
                      " display_name = EXCLUDED.display_name,"
                      " linked_at = EXCLUDED.linked_at,"
                      " provider_token = EXCLUDED.provider_token",
                      7, params);
    int result;
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        result = MS_DB_OK;
    else if (sqlstate_is(r, "23505"))
        result = MS_DB_CONFLICT_LINK;
    else
        result = log_failure("link_put", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

// the rows of the other tables go with it (ON DELETE CASCADE)
static int pg_account_delete(struct ms_db *db, char const *uuid)
{
    char const *params[] = {uuid};
    return pg_simple(db, "account_delete",
                     "DELETE FROM accounts.accounts WHERE uuid = $1", 1, params);
}

static int pg_link_delete(struct ms_db *db, char const *uuid,
                          char const *provider)
{
    char const *params[] = {uuid, provider};
    return pg_simple(db, "link_delete",
                     "DELETE FROM accounts.links WHERE uuid = $1"
                     " AND provider = $2",
                     2, params);
}

static int pg_links_get(struct ms_db *db, char const *uuid,
                        struct ms_db_link *out, int max)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char const *params[] = {uuid};
    PGresult *r = run(c,
                      "SELECT provider, provider_user_id, provider_email,"
                      " display_name, linked_at, provider_token FROM accounts.links"
                      " WHERE uuid = $1 ORDER BY provider",
                      1, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("links_get", r);
    else
    {
        int n = PQntuples(r);
        if (n > max)
            n = max;
        for (int i = 0; i < n; ++i)
        {
            memset(&out[i], 0, sizeof out[i]);
            ms_strlcpy(out[i].provider, PQgetvalue(r, i, 0),
                       sizeof out[i].provider);
            ms_strlcpy(out[i].provider_user_id, PQgetvalue(r, i, 1),
                       sizeof out[i].provider_user_id);
            ms_strlcpy(out[i].provider_email, PQgetvalue(r, i, 2),
                       sizeof out[i].provider_email);
            ms_strlcpy(out[i].display_name, PQgetvalue(r, i, 3),
                       sizeof out[i].display_name);
            out[i].linked_at = atoll(PQgetvalue(r, i, 4));
            ms_strlcpy(out[i].token, PQgetvalue(r, i, 5), sizeof out[i].token);
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_parteria_get(struct ms_db *db, char const *uuid,
                           struct ms_db_parteria *out)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char const *params[] = {uuid};
    PGresult *r = run(c,
                      "SELECT xp, checkpoint, pvp_points, petals::text,"
                      " failed_crafts::text, mob_kills::text, loadout::text,"
                      " nickname FROM rysteria.players WHERE uuid = $1",
                      1, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("parteria_get", r);
    else if (PQntuples(r) == 0)
        result = MS_DB_NOT_FOUND;
    else
    {
        ms_db_parteria_free(out);
        out->xp = atof(PQgetvalue(r, 0, 0));
        out->checkpoint = atoi(PQgetvalue(r, 0, 1));
        out->pvp_points = strtoll(PQgetvalue(r, 0, 2), NULL, 10);
        out->petals = strdup(PQgetvalue(r, 0, 3));
        out->failed_crafts = strdup(PQgetvalue(r, 0, 4));
        out->mob_kills = strdup(PQgetvalue(r, 0, 5));
        out->loadout = strdup(PQgetvalue(r, 0, 6));
        ms_strlcpy(out->nickname, PQgetvalue(r, 0, 7), sizeof out->nickname);
        result = MS_DB_OK;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_parteria_put(struct ms_db *db, char const *uuid,
                           struct ms_db_parteria const *v)
{
    char xp[64];
    snprintf(xp, sizeof xp, "%.17g", v->xp);
    char checkpoint[16];
    snprintf(checkpoint, sizeof checkpoint, "%d", v->checkpoint);
    char pvp_points[32];
    snprintf(pvp_points, sizeof pvp_points, "%lld", (long long)v->pvp_points);
    char updated[32];
    fmt_i64(updated, sizeof updated, ms_now());
    char const *params[] = {uuid,           xp,        checkpoint, pvp_points,
                            v->petals,      v->failed_crafts, v->mob_kills,
                            v->loadout,     v->nickname,      updated};
    return pg_simple(db, "parteria_put",
                     "INSERT INTO rysteria.players (uuid, xp, checkpoint,"
                     " pvp_points, petals, failed_crafts, mob_kills, loadout,"
                     " nickname, updated_at) VALUES ($1,$2::double precision,"
                     " $3::integer,$4::bigint,$5::jsonb,$6::jsonb,$7::jsonb,"
                     " $8::jsonb,$9,$10::bigint)"
                     " ON CONFLICT (uuid) DO UPDATE SET xp = EXCLUDED.xp,"
                     " checkpoint = EXCLUDED.checkpoint,"
                     " pvp_points = EXCLUDED.pvp_points,"
                     " petals = EXCLUDED.petals,"
                     " failed_crafts = EXCLUDED.failed_crafts,"
                     " mob_kills = EXCLUDED.mob_kills,"
                     " loadout = EXCLUDED.loadout,"
                     " nickname = EXCLUDED.nickname,"
                     " updated_at = EXCLUDED.updated_at",
                     10, params);
}

static int pg_gardn_list(struct ms_db *db, char const *uuid,
                         struct ms_db_gsave *out, int max)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char const *params[] = {uuid};
    PGresult *r = run(c,
                      "SELECT save_id, score, loadout::text, created_at"
                      " FROM gardn.saves WHERE uuid = $1"
                      " ORDER BY created_at DESC, save_id",
                      1, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("gardn_list", r);
    else
    {
        int n = PQntuples(r);
        if (n > max)
            n = max;
        for (int i = 0; i < n; ++i)
        {
            memset(&out[i], 0, sizeof out[i]);
            ms_strlcpy(out[i].save_id, PQgetvalue(r, i, 0),
                       sizeof out[i].save_id);
            out[i].score = (uint32_t)atoll(PQgetvalue(r, i, 1));
            out[i].loadout = strdup(PQgetvalue(r, i, 2));
            out[i].created_at = atoll(PQgetvalue(r, i, 3));
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_gardn_take(struct ms_db *db, char const *uuid, char const *save_id,
                         struct ms_db_gsave *out)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char const *params[] = {uuid, save_id};
    PGresult *r = run(c,
                      "DELETE FROM gardn.saves WHERE uuid = $1 AND save_id = $2"
                      " RETURNING save_id, score, loadout::text, created_at",
                      2, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("gardn_take", r);
    else if (PQntuples(r) == 0)
        result = MS_DB_NOT_FOUND;
    else
    {
        memset(out, 0, sizeof *out);
        ms_strlcpy(out->save_id, PQgetvalue(r, 0, 0), sizeof out->save_id);
        out->score = (uint32_t)atoll(PQgetvalue(r, 0, 1));
        out->loadout = strdup(PQgetvalue(r, 0, 2));
        out->created_at = atoll(PQgetvalue(r, 0, 3));
        result = MS_DB_OK;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_gardn_apply(struct ms_db *db, char const *uuid,
                          struct ms_db_gsave const *add, int add_count,
                          char const (*remove)[40], int remove_count)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    int result = MS_DB_OK;
    PGresult *r = PQexec(c, "BEGIN");
    if (!is_command_ok(r))
        result = log_failure("gardn_apply begin", r);
    PQclear(r);
    for (int i = 0; result == MS_DB_OK && i < add_count; ++i)
    {
        char score[32];
        fmt_i64(score, sizeof score, add[i].score);
        char created[32];
        fmt_i64(created, sizeof created,
                add[i].created_at ? add[i].created_at : ms_now());
        char const *params[] = {uuid, add[i].save_id, score, add[i].loadout,
                                created};
        r = run(c,
                "INSERT INTO gardn.saves (uuid, save_id, score, loadout,"
                " created_at) VALUES ($1,$2,$3::bigint,$4::jsonb,$5::bigint)"
                " ON CONFLICT (uuid, save_id) DO UPDATE SET"
                " score = EXCLUDED.score, loadout = EXCLUDED.loadout",
                5, params);
        if (!is_command_ok(r))
            result = log_failure("gardn_apply insert", r);
        PQclear(r);
    }
    for (int i = 0; result == MS_DB_OK && i < remove_count; ++i)
    {
        char const *params[] = {uuid, remove[i]};
        r = run(c, "DELETE FROM gardn.saves WHERE uuid = $1 AND save_id = $2",
                2, params);
        if (!is_command_ok(r))
            result = log_failure("gardn_apply delete", r);
        PQclear(r);
    }
    if (result == MS_DB_OK)
    {
        char limit[16];
        snprintf(limit, sizeof limit, "%d", MS_GARDN_MAX_SAVES);
        char const *params[] = {uuid, limit};
        r = run(c,
                "DELETE FROM gardn.saves WHERE uuid = $1 AND save_id NOT IN"
                " (SELECT save_id FROM gardn.saves WHERE uuid = $1"
                "  ORDER BY created_at DESC, save_id LIMIT $2::int)",
                2, params);
        if (!is_command_ok(r))
            result = log_failure("gardn_apply trim", r);
        PQclear(r);
    }
    r = PQexec(c, result == MS_DB_OK ? "COMMIT" : "ROLLBACK");
    if (!is_command_ok(r) && result == MS_DB_OK)
        result = log_failure("gardn_apply commit", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_gardn_backup_replace(struct ms_db *db,
                                   struct ms_db_backup const *in, int count)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    int result = MS_DB_OK;
    PGresult *r = PQexec(c, "BEGIN");
    if (!is_command_ok(r))
        result = log_failure("gardn_backup begin", r);
    PQclear(r);
    char now[32];
    fmt_i64(now, sizeof now, ms_now());
    for (int i = 0; result == MS_DB_OK && i < count; ++i)
    {
        char score[32];
        fmt_i64(score, sizeof score, in[i].score);
        char const *params[] = {in[i].uuid, score, in[i].loadout, now};
        // written only when it changed, so that the same row is not written every
        // 30 seconds; an account that is gone is skipped
        r = run(c,
                "INSERT INTO gardn.crash_backups (uuid, score, loadout,"
                " updated_at) SELECT $1::text, $2::bigint, $3::jsonb,"
                " $4::bigint WHERE EXISTS (SELECT 1 FROM accounts.accounts"
                " WHERE uuid = $1::text)"
                " ON CONFLICT (uuid) DO UPDATE SET score = EXCLUDED.score,"
                " loadout = EXCLUDED.loadout, updated_at = EXCLUDED.updated_at"
                " WHERE gardn.crash_backups.score <> EXCLUDED.score"
                " OR gardn.crash_backups.loadout <> EXCLUDED.loadout",
                4, params);
        if (!is_command_ok(r))
            result = log_failure("gardn_backup upsert", r);
        PQclear(r);
    }
    if (result == MS_DB_OK)
    {
        // everyone who was not reported this time has left (or the gardn
        // server stopped seeing them): off the table
        if (count == 0)
            r = run(c, "DELETE FROM gardn.crash_backups", 0, NULL);
        else
        {
            size_t size = (size_t)count * (MS_UUID_SIZE + 3) + 3;
            char *list = malloc(size);
            size_t at = 0;
            list[at++] = '{';
            for (int i = 0; i < count; ++i)
                at += (size_t)snprintf(list + at, size - at, "%s\"%s\"",
                                       i ? "," : "", in[i].uuid);
            list[at++] = '}';
            list[at] = 0;
            char const *params[] = {list};
            r = run(c,
                    "DELETE FROM gardn.crash_backups WHERE uuid <> ALL($1::text[])",
                    1, params);
            free(list);
        }
        if (!is_command_ok(r))
            result = log_failure("gardn_backup delete", r);
        PQclear(r);
    }
    r = PQexec(c, result == MS_DB_OK ? "COMMIT" : "ROLLBACK");
    if (!is_command_ok(r) && result == MS_DB_OK)
        result = log_failure("gardn_backup commit", r);
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_gardn_backup_restore(struct ms_db *db, struct ms_db_backup *out,
                                   int max)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    int result = MS_DB_OK;
    int moved = 0;
    PGresult *r = PQexec(c, "BEGIN");
    if (!is_command_ok(r))
        result = log_failure("gardn_backup_restore begin", r);
    PQclear(r);
    char now[32];
    fmt_i64(now, sizeof now, ms_now());
    if (result == MS_DB_OK)
    {
        char const *params[] = {now};
        // one statement: the rows leave the backups and arrive as saves together
        r = run(c,
                "WITH moved AS (DELETE FROM gardn.crash_backups"
                " RETURNING uuid, score, loadout),"
                " ins AS (INSERT INTO gardn.saves (uuid, save_id, score,"
                "  loadout, created_at)"
                "  SELECT uuid, substr(md5(random()::text ||"
                "   clock_timestamp()::text || uuid), 1, 16), score, loadout,"
                "   $1::bigint FROM moved WHERE score > 0"
                "  ON CONFLICT DO NOTHING RETURNING uuid, score)"
                " SELECT uuid, score FROM ins",
                1, params);
        if (PQresultStatus(r) != PGRES_TUPLES_OK)
            result = log_failure("gardn_backup_restore", r);
        else
        {
            moved = PQntuples(r);
            for (int i = 0; i < moved && i < max; ++i)
            {
                memset(&out[i], 0, sizeof out[i]);
                ms_strlcpy(out[i].uuid, PQgetvalue(r, i, 0), sizeof out[i].uuid);
                out[i].score = (uint32_t)atoll(PQgetvalue(r, i, 1));
            }
        }
        PQclear(r);
    }
    // the same limit as every other way of adding a save: the oldest go
    for (int i = 0; result == MS_DB_OK && i < moved && i < max; ++i)
    {
        char limit[16];
        snprintf(limit, sizeof limit, "%d", MS_GARDN_MAX_SAVES);
        char const *params[] = {out[i].uuid, limit};
        r = run(c,
                "DELETE FROM gardn.saves WHERE uuid = $1 AND save_id NOT IN"
                " (SELECT save_id FROM gardn.saves WHERE uuid = $1"
                "  ORDER BY created_at DESC, save_id LIMIT $2::int)",
                2, params);
        if (!is_command_ok(r))
            result = log_failure("gardn_backup_restore trim", r);
        PQclear(r);
    }
    r = PQexec(c, result == MS_DB_OK ? "COMMIT" : "ROLLBACK");
    if (!is_command_ok(r) && result == MS_DB_OK)
        result = log_failure("gardn_backup_restore commit", r);
    PQclear(r);
    release(pg, slot);
    return result == MS_DB_OK ? moved : MS_DB_ERROR;
}

static int pg_lb_submit(struct ms_db *db, char const *board, char const *uuid,
                        double score, char const *extra_json)
{
    char s[64];
    snprintf(s, sizeof s, "%.17g", score);
    char updated[32];
    fmt_i64(updated, sizeof updated, ms_now());
    char const *params[] = {board, uuid, s, extra_json ? extra_json : "{}",
                            updated};
    return pg_simple(db, "lb_submit",
                     "INSERT INTO leaderboards.entries (board, uuid, score,"
                     " extra, updated_at) VALUES ($1,$2,$3::double precision,"
                     " $4::jsonb,$5::bigint)"
                     " ON CONFLICT (board, uuid) DO UPDATE SET"
                     " score = GREATEST(leaderboards.entries.score,"
                     " EXCLUDED.score), extra = EXCLUDED.extra,"
                     " updated_at = EXCLUDED.updated_at",
                     5, params);
}

static int pg_lb_top(struct ms_db *db, char const *board, int limit,
                     struct ms_db_lb_entry *out)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char lim[16];
    snprintf(lim, sizeof lim, "%d", limit);
    char const *params[] = {board, lim};
    PGresult *r = run(c,
                      "SELECT uuid, score, extra::text FROM"
                      " leaderboards.entries WHERE board = $1 AND score > 0"
                      " ORDER BY score DESC, updated_at ASC, uuid ASC"
                      " LIMIT $2::int",
                      2, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("lb_top", r);
    else
    {
        int n = PQntuples(r);
        if (n > limit)
            n = limit;
        for (int i = 0; i < n; ++i)
        {
            memset(&out[i], 0, sizeof out[i]);
            ms_strlcpy(out[i].uuid, PQgetvalue(r, i, 0), sizeof out[i].uuid);
            out[i].score = atof(PQgetvalue(r, i, 1));
            out[i].extra = strdup(PQgetvalue(r, i, 2));
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_gardn_petal_stats(struct ms_db *db, struct ms_db_petal_stat *out,
                                int max, int64_t *saves, int64_t *players)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    int result;
    PGresult *r = run(c,
                      "SELECT count(*), count(DISTINCT uuid) FROM gardn.saves",
                      0, NULL);
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("gardn_petal_stats totals", r);
    else
    {
        *saves = atoll(PQgetvalue(r, 0, 0));
        *players = atoll(PQgetvalue(r, 0, 1));
        PQclear(r);
        char lim[16];
        snprintf(lim, sizeof lim, "%d", max);
        char const *params[] = {lim};
        r = run(c,
                "SELECT e.v::int, count(*), count(DISTINCT (s.uuid, s.save_id))"
                " FROM gardn.saves s,"
                " LATERAL jsonb_array_elements_text(s.loadout) AS e(v)"
                " WHERE e.v ~ '^[1-9][0-9]{0,4}$'"
                " GROUP BY 1 ORDER BY 2 DESC, 1 ASC LIMIT $1::int",
                1, params);
        if (PQresultStatus(r) != PGRES_TUPLES_OK)
            result = log_failure("gardn_petal_stats", r);
        else
        {
            int n = PQntuples(r);
            if (n > max)
                n = max;
            for (int i = 0; i < n; ++i)
            {
                out[i].id = atoi(PQgetvalue(r, i, 0));
                out[i].copies = atoll(PQgetvalue(r, i, 1));
                out[i].saves = atoll(PQgetvalue(r, i, 2));
            }
            result = n;
        }
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

// the json of a column that is stored as jsonb, "{}" when there is none or it is not json
static void add_json_column(cJSON *o, char const *key, char const *text)
{
    cJSON *v = text != NULL ? cJSON_Parse(text) : NULL;
    cJSON_AddItemToObject(o, key, v != NULL ? v : cJSON_CreateObject());
}

static int pg_parteria_put_many(struct ms_db *db,
                                struct ms_db_parteria_row const *rows, int count)
{
    if (count <= 0)
        return MS_DB_OK;
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < count; ++i)
    {
        struct ms_db_parteria const *v = rows[i].v;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uuid", rows[i].uuid);
        cJSON_AddNumberToObject(o, "xp", v->xp);
        cJSON_AddNumberToObject(o, "checkpoint", v->checkpoint);
        cJSON_AddNumberToObject(o, "pvp_points", (double)v->pvp_points);
        add_json_column(o, "petals", v->petals);
        add_json_column(o, "failed_crafts", v->failed_crafts);
        add_json_column(o, "mob_kills", v->mob_kills);
        add_json_column(o, "loadout", v->loadout);
        cJSON_AddStringToObject(o, "nickname", v->nickname);
        cJSON_AddItemToArray(list, o);
    }
    char *json = cJSON_PrintUnformatted(list);
    cJSON_Delete(list);
    if (json == NULL)
        return MS_DB_ERROR;
    char updated[32];
    fmt_i64(updated, sizeof updated, ms_now());
    char const *params[] = {json, updated};
    int result = pg_simple(
        db, "parteria_put_many",
        "INSERT INTO rysteria.players (uuid, xp, checkpoint, pvp_points, petals,"
        " failed_crafts, mob_kills, loadout, nickname, updated_at)"
        " SELECT d.uuid, d.xp, d.checkpoint, d.pvp_points, d.petals,"
        " d.failed_crafts, d.mob_kills, d.loadout, d.nickname, $2::bigint"
        " FROM jsonb_to_recordset($1::jsonb) AS d(uuid text, xp double precision,"
        " checkpoint integer, pvp_points bigint, petals jsonb,"
        " failed_crafts jsonb, mob_kills jsonb, loadout jsonb, nickname text)"
        " ON CONFLICT (uuid) DO UPDATE SET xp = EXCLUDED.xp,"
        " checkpoint = EXCLUDED.checkpoint, pvp_points = EXCLUDED.pvp_points,"
        " petals = EXCLUDED.petals, failed_crafts = EXCLUDED.failed_crafts,"
        " mob_kills = EXCLUDED.mob_kills, loadout = EXCLUDED.loadout,"
        " nickname = EXCLUDED.nickname, updated_at = EXCLUDED.updated_at",
        2, params);
    free(json);
    return result;
}

static int pg_lb_apply_many(struct ms_db *db, struct ms_db_lb_op const *ops,
                            int count)
{
    cJSON *list = cJSON_CreateArray();
    int used = 0;
    for (int i = 0; i < count; ++i)
    {
        // the same two cases as lb_apply: a "set" of 0 or less takes the player
        // off the board, the other modes have nothing to do with it
        if (ops[i].mode != ms_lb_set && ops[i].value <= 0)
            continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "board", ops[i].board);
        cJSON_AddStringToObject(o, "uuid", ops[i].uuid);
        cJSON_AddNumberToObject(o, "score", ops[i].value);
        add_json_column(o, "extra", ops[i].extra_json[0] ? ops[i].extra_json : "{}");
        cJSON_AddStringToObject(o, "mode",
                                ops[i].mode == ms_lb_set   ? "set"
                                : ops[i].mode == ms_lb_max ? "max"
                                                           : "add");
        cJSON_AddItemToArray(list, o);
        ++used;
    }
    if (used == 0)
    {
        cJSON_Delete(list);
        return MS_DB_OK;
    }
    char *json = cJSON_PrintUnformatted(list);
    cJSON_Delete(list);
    if (json == NULL)
        return MS_DB_ERROR;
    char updated[32];
    fmt_i64(updated, sizeof updated, ms_now());
    char const *params[] = {json, updated};
    // updated_at only moves when the score does (see lb_apply). The mode of the
    // row that is being written is looked up in the list, since EXCLUDED has no
    // column for it
#define LB_MANY_NEW_SCORE                                                      \
    "(CASE (SELECT m.mode FROM d m WHERE m.board = leaderboards.entries.board" \
    " AND m.uuid = leaderboards.entries.uuid)"                                 \
    " WHEN 'set' THEN EXCLUDED.score"                                          \
    " WHEN 'max' THEN GREATEST(leaderboards.entries.score, EXCLUDED.score)"    \
    " ELSE leaderboards.entries.score + EXCLUDED.score END)"
    int result = pg_simple(
        db, "lb_apply_many",
        "WITH d AS (SELECT * FROM jsonb_to_recordset($1::jsonb) AS x(board text,"
        " uuid text, score double precision, extra jsonb, mode text)),"
        " gone AS (DELETE FROM leaderboards.entries e USING d"
        " WHERE d.mode = 'set' AND d.score <= 0"
        " AND e.board = d.board AND e.uuid = d.uuid)"
        " INSERT INTO leaderboards.entries (board, uuid, score, extra, updated_at)"
        " SELECT d.board, d.uuid, d.score, d.extra, $2::bigint FROM d"
        " WHERE d.score > 0"
        " ON CONFLICT (board, uuid) DO UPDATE SET"
        " updated_at = CASE WHEN " LB_MANY_NEW_SCORE
        " = leaderboards.entries.score THEN leaderboards.entries.updated_at"
        " ELSE EXCLUDED.updated_at END,"
        " score = " LB_MANY_NEW_SCORE ", extra = EXCLUDED.extra",
        2, params);
#undef LB_MANY_NEW_SCORE
    free(json);
    return result;
}

static int pg_lb_apply(struct ms_db *db, char const *board, char const *uuid,
                       double value, enum ms_lb_mode mode,
                       char const *extra_json)
{
    if (mode == ms_lb_set && value <= 0)
    {
        char const *params[] = {board, uuid};
        return pg_simple(db, "lb_apply delete",
                         "DELETE FROM leaderboards.entries"
                         " WHERE board = $1 AND uuid = $2",
                         2, params);
    }
    if (mode != ms_lb_set && value <= 0)
        return MS_DB_OK; // nothing to add, nothing to raise
    char s[64];
    snprintf(s, sizeof s, "%.17g", value);
    char updated[32];
    fmt_i64(updated, sizeof updated, ms_now());
    char const *mode_text = mode == ms_lb_set ? "set"
                            : mode == ms_lb_max ? "max" : "add";
    char const *params[] = {board, uuid, s, extra_json ? extra_json : "{}",
                            updated, mode_text};
    // updated_at only moves when the score does: it decides who is ahead of an
    // equal score (the one who got there first)
#define LB_NEW_SCORE                                                           \
    "(CASE $6::text WHEN 'set' THEN EXCLUDED.score"                            \
    " WHEN 'max' THEN GREATEST(leaderboards.entries.score, EXCLUDED.score)"    \
    " ELSE leaderboards.entries.score + EXCLUDED.score END)"
    return pg_simple(db, "lb_apply",
                     "INSERT INTO leaderboards.entries (board, uuid, score,"
                     " extra, updated_at) VALUES ($1,$2,$3::double precision,"
                     " $4::jsonb,$5::bigint)"
                     " ON CONFLICT (board, uuid) DO UPDATE SET"
                     " updated_at = CASE WHEN " LB_NEW_SCORE
                     " = leaderboards.entries.score THEN"
                     " leaderboards.entries.updated_at ELSE EXCLUDED.updated_at"
                     " END, score = " LB_NEW_SCORE ", extra = EXCLUDED.extra",
                     6, params);
#undef LB_NEW_SCORE
}

static int pg_lb_rank(struct ms_db *db, char const *board, char const *uuid,
                      double *score, int64_t *rank)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char const *params[] = {board, uuid};
    PGresult *r = run(
        c,
        "SELECT e.score, (SELECT count(*) FROM leaderboards.entries o"
        " WHERE o.board = e.board AND o.score > 0 AND (o.score > e.score OR"
        " (o.score = e.score AND (o.updated_at < e.updated_at OR"
        " (o.updated_at = e.updated_at AND o.uuid < e.uuid))))) + 1"
        " FROM leaderboards.entries e"
        " WHERE e.board = $1 AND e.uuid = $2 AND e.score > 0",
        2, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("lb_rank", r);
    else if (PQntuples(r) == 0)
        result = MS_DB_NOT_FOUND;
    else
    {
        *score = atof(PQgetvalue(r, 0, 0));
        *rank = atoll(PQgetvalue(r, 0, 1));
        result = MS_DB_OK;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_account_page(struct ms_db *db, int offset, int limit,
                           struct ms_db_account_ref *out)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char off[16], lim[16];
    snprintf(off, sizeof off, "%d", offset);
    snprintf(lim, sizeof lim, "%d", limit);
    char const *params[] = {lim, off};
    PGresult *r = run(c,
                      "SELECT uuid, username, is_dev FROM accounts.accounts"
                      " ORDER BY uuid LIMIT $1::int OFFSET $2::int",
                      2, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("account_page", r);
    else
    {
        int n = PQntuples(r);
        if (n > limit)
            n = limit;
        for (int i = 0; i < n; ++i)
        {
            memset(&out[i], 0, sizeof out[i]);
            ms_strlcpy(out[i].uuid, PQgetvalue(r, i, 0), sizeof out[i].uuid);
            ms_strlcpy(out[i].username, PQgetvalue(r, i, 1),
                       sizeof out[i].username);
            out[i].is_dev = PQgetvalue(r, i, 2)[0] == 't';
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_account_search(struct ms_db *db, char const *prefix, int limit,
                             struct ms_db_account_ref *out)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    // the prefix only holds username characters; _ is a wildcard of LIKE
    char pattern[160];
    size_t o = 0;
    for (char const *p = prefix; *p && o + 3 < sizeof pattern; ++p)
    {
        if (*p == '_' || *p == '%' || *p == '\\')
            pattern[o++] = '\\';
        pattern[o++] = *p;
    }
    pattern[o++] = '%';
    pattern[o] = 0;
    char lim[16];
    snprintf(lim, sizeof lim, "%d", limit);
    char const *params[] = {pattern, lim};
    PGresult *r = run(c,
                      "SELECT uuid, username, is_dev FROM accounts.accounts"
                      " WHERE username_lower LIKE $1 AND NOT is_dev"
                      " ORDER BY username_lower LIMIT $2::int",
                      2, params);
    int result;
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
        result = log_failure("account_search", r);
    else
    {
        int n = PQntuples(r);
        if (n > limit)
            n = limit;
        for (int i = 0; i < n; ++i)
        {
            memset(&out[i], 0, sizeof out[i]);
            ms_strlcpy(out[i].uuid, PQgetvalue(r, i, 0), sizeof out[i].uuid);
            ms_strlcpy(out[i].username, PQgetvalue(r, i, 1),
                       sizeof out[i].username);
            out[i].is_dev = 0;
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_ping(struct ms_db *db) { return pg_simple(db, "ping", "SELECT 1", 0, NULL); }

static void pg_close(struct ms_db *db)
{
    struct pg *pg = db->ctx;
    for (int i = 0; i < pg->count; ++i)
        PQfinish(pg->conns[i]);
    free(pg->conns);
    free(pg->busy);
    free(pg);
    free(db);
}

struct ms_db *ms_db_postgres_open()
{
    struct pg *pg = calloc(1, sizeof *pg);
    int count = (int)MS_CONFIG.db_pool_size;
    pg->conns = calloc((size_t)count, sizeof *pg->conns);
    pg->busy = calloc((size_t)count, sizeof *pg->busy);
    pthread_mutex_init(&pg->lock, NULL);
    pthread_cond_init(&pg->freed, NULL);
    for (int i = 0; i < count; ++i)
    {
        PGconn *c = PQconnectdb(MS_CONFIG.postgres_uri);
        if (PQstatus(c) != CONNECTION_OK)
        {
            ms_log("db", "postgres connect failed: %s", PQerrorMessage(c));
            PQfinish(c);
            for (int j = 0; j < i; ++j)
                PQfinish(pg->conns[j]);
            free(pg->conns);
            free(pg->busy);
            free(pg);
            return NULL;
        }
        pg->conns[i] = c;
        pg->count = i + 1;
    }

    // serialise concurrent bootstraps of several processes
    PGresult *r = PQexec(pg->conns[0], "SELECT pg_advisory_lock(7203947)");
    PQclear(r);
    r = PQexec(pg->conns[0], SCHEMA_SQL);
    int ok = PQresultStatus(r) == PGRES_COMMAND_OK;
    if (!ok)
        ms_log("db", "postgres schema bootstrap failed: %s",
               PQresultErrorMessage(r));
    PQclear(r);
    r = PQexec(pg->conns[0], "SELECT pg_advisory_unlock(7203947)");
    PQclear(r);
    if (!ok)
    {
        for (int i = 0; i < pg->count; ++i)
            PQfinish(pg->conns[i]);
        free(pg->conns);
        free(pg->busy);
        free(pg);
        return NULL;
    }

    struct ms_db *db = calloc(1, sizeof *db);
    db->name = "postgres";
    db->ctx = pg;
    db->account_create = pg_account_create;
    db->account_get = pg_account_get;
    db->account_set_credentials = pg_account_set_credentials;
    db->account_set_last_login = pg_account_set_last_login;
    db->account_set_email_verified = pg_account_set_email_verified;
    db->account_set_email = pg_account_set_email;
    db->account_set_username = pg_account_set_username;
    db->link_put = pg_link_put;
    db->link_delete = pg_link_delete;
    db->account_delete = pg_account_delete;
    db->links_get = pg_links_get;
    db->parteria_get = pg_parteria_get;
    db->parteria_put = pg_parteria_put;
    db->parteria_put_many = pg_parteria_put_many;
    db->gardn_list = pg_gardn_list;
    db->gardn_apply = pg_gardn_apply;
    db->gardn_take = pg_gardn_take;
    db->gardn_backup_replace = pg_gardn_backup_replace;
    db->gardn_backup_restore = pg_gardn_backup_restore;
    db->lb_submit = pg_lb_submit;
    db->lb_top = pg_lb_top;
    db->lb_apply = pg_lb_apply;
    db->lb_apply_many = pg_lb_apply_many;
    db->lb_rank = pg_lb_rank;
    db->gardn_petal_stats = pg_gardn_petal_stats;
    db->account_page = pg_account_page;
    db->account_search = pg_account_search;
    db->ping = pg_ping;
    db->close = pg_close;
    ms_log("db", "postgres ready (%d connections)", count);
    return db;
}

#endif // MS_WITH_POSTGRES
