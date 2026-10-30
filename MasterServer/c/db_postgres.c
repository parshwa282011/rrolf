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
    "CREATE TABLE IF NOT EXISTS leaderboards.entries ("
    " board text NOT NULL,"
    " uuid text NOT NULL REFERENCES accounts.accounts (uuid) ON DELETE CASCADE,"
    " score double precision NOT NULL,"
    " extra jsonb NOT NULL DEFAULT '{}',"
    " updated_at bigint NOT NULL,"
    " PRIMARY KEY (board, uuid));"
    "CREATE INDEX IF NOT EXISTS leaderboards_entries_score_idx"
    " ON leaderboards.entries (board, score DESC);"
    // ---- change log for the backup servers (see pg_backup) ----
    "CREATE SCHEMA IF NOT EXISTS sync;"
    "CREATE TABLE IF NOT EXISTS sync.meta ("
    " key text PRIMARY KEY, value text NOT NULL);"
    "INSERT INTO sync.meta (key, value) VALUES"
    " ('source_id', gen_random_uuid()::text), ('pruned_before', '0')"
    " ON CONFLICT DO NOTHING;"
    "CREATE TABLE IF NOT EXISTS sync.changes ("
    " id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,"
    " xid bigint NOT NULL DEFAULT (pg_current_xact_id()::text::bigint),"
    " at timestamptz NOT NULL DEFAULT now(),"
    " tbl text NOT NULL, op char(1) NOT NULL, data jsonb NOT NULL);"
    "CREATE INDEX IF NOT EXISTS changes_xid_idx ON sync.changes (xid);"
    // A backup server applying a feed sets sync.applying so its own copy does
    // not log what it is receiving.
    "CREATE OR REPLACE FUNCTION sync.capture() RETURNS trigger"
    " LANGUAGE plpgsql AS $f$ BEGIN"
    " IF current_setting('sync.applying', true) = '1' THEN RETURN NULL; END IF;"
    " IF TG_OP = 'DELETE' THEN"
    "  INSERT INTO sync.changes (tbl, op, data) VALUES"
    "   (TG_TABLE_SCHEMA || '.' || TG_TABLE_NAME, 'D', to_jsonb(OLD));"
    " ELSE"
    "  INSERT INTO sync.changes (tbl, op, data) VALUES"
    "   (TG_TABLE_SCHEMA || '.' || TG_TABLE_NAME, 'U', to_jsonb(NEW));"
    " END IF;"
    " RETURN NULL; END $f$;"
    "DO $f$ DECLARE t text; BEGIN"
    " FOR t IN SELECT format('%I.%I', schemaname, tablename) FROM pg_tables"
    "  WHERE schemaname IN ('accounts', 'rysteria', 'gardn', 'leaderboards')"
    " LOOP"
    "  IF NOT EXISTS (SELECT 1 FROM pg_trigger WHERE tgrelid = t::regclass"
    "   AND tgname = 'sync_capture') THEN"
    "   EXECUTE format('CREATE TRIGGER sync_capture AFTER INSERT OR UPDATE OR"
    "    DELETE ON %s FOR EACH ROW EXECUTE FUNCTION sync.capture()', t);"
    "  END IF;"
    " END LOOP; END $f$;";

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
        " a.token_key, a.is_dev, a.created_at, a.last_login_at"
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

static int pg_link_put(struct ms_db *db, char const *uuid,
                       struct ms_db_link const *l)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    char linked[32];
    fmt_i64(linked, sizeof linked, l->linked_at);
    char const *params[] = {uuid,           l->provider,     l->provider_user_id,
                            l->provider_email, l->display_name, linked};
    PGresult *r = run(c,
                      "INSERT INTO accounts.links (uuid, provider,"
                      " provider_user_id, provider_email, display_name,"
                      " linked_at) VALUES ($1,$2,$3,$4,$5,$6)"
                      " ON CONFLICT (uuid, provider) DO UPDATE SET"
                      " provider_user_id = EXCLUDED.provider_user_id,"
                      " provider_email = EXCLUDED.provider_email,"
                      " display_name = EXCLUDED.display_name,"
                      " linked_at = EXCLUDED.linked_at",
                      6, params);
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
                      " display_name, linked_at FROM accounts.links"
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
        }
        result = n;
    }
    PQclear(r);
    release(pg, slot);
    return result;
}

static int pg_rysteria_get(struct ms_db *db, char const *uuid,
                           struct ms_db_rysteria *out)
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
        result = log_failure("rysteria_get", r);
    else if (PQntuples(r) == 0)
        result = MS_DB_NOT_FOUND;
    else
    {
        ms_db_rysteria_free(out);
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

static int pg_rysteria_put(struct ms_db *db, char const *uuid,
                           struct ms_db_rysteria const *v)
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
    return pg_simple(db, "rysteria_put",
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
                      " leaderboards.entries WHERE board = $1"
                      " ORDER BY score DESC LIMIT $2::int",
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

// ---------------------------------------------------------------- backup feed
//
// Every write to the data tables is logged by a trigger into sync.changes
// together with the id of its transaction (xid). A backup server pulls
//   * everything (mode "snapshot"): the first time, after a failover (another
//     source id), or when its cursor is older than the history we keep;
//   * the changes since its cursor (mode "incremental").
// The cursor is "the oldest transaction id not read yet". A pull only returns
// transactions older than the oldest one still running (the snapshot's xmin),
// so a slow transaction is never skipped, and returns that xmin as the next
// cursor. Rows are replayed in log order, which is the commit order per row.
// A snapshot is read in the same REPEATABLE READ transaction as its cursor;
// changes that were already inside it are simply applied again (idempotent).
//
// Document (newline delimited json):
//   {"type":"head","source":..,"node":..,"mode":..,"next":"<cursor>","tables":[..]}
//   {"t":"schema.table","op":"U"|"D","row":{..}}      repeated
//   {"type":"end","count":<rows>}
// "tables" (snapshot only) are in foreign key order, parents first.

#define BACKUP_KEEP "3 days"

struct sbuf
{
    char *p;
    size_t len, cap;
};

static int sb_add(struct sbuf *b, char const *s, size_t n)
{
    if (b->len + n + 1 > b->cap)
    {
        size_t cap = b->cap ? b->cap : 65536;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *p = realloc(b->p, cap);
        if (p == NULL)
            return -1;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}

static int sb_line(struct sbuf *b, char const *s)
{
    return sb_add(b, s, strlen(s)) || sb_add(b, "\n", 1);
}

static int digits_only(char const *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 18)
        return 0;
    for (; *s; ++s)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

static int ident_ok(char const *s)
{
    if (*s == 0)
        return 0;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_')
            return 0;
    return 1;
}

// Hourly: drops log rows older than BACKUP_KEEP. pruned_before then names the
// first transaction a cursor may still start from.
static void prune_changes(PGconn *c)
{
    static int64_t last;
    int64_t now = ms_now();
    if (now - last < 3600)
        return;
    last = now;
    PGresult *r = PQexec(
        c,
        "WITH keep AS (SELECT min(xid) AS x FROM sync.changes"
        "  WHERE at >= now() - interval '" BACKUP_KEEP "'),"
        " del AS (DELETE FROM sync.changes"
        "  WHERE at < now() - interval '" BACKUP_KEEP "'"
        "  AND (xid < (SELECT x FROM keep) OR (SELECT x FROM keep) IS NULL)"
        "  RETURNING xid)"
        " UPDATE sync.meta SET value = GREATEST(value::bigint,"
        "  (SELECT max(xid) FROM del) + 1)::text"
        " WHERE key = 'pruned_before' AND EXISTS (SELECT 1 FROM del)");
    if (PQresultStatus(r) != PGRES_COMMAND_OK)
        log_failure("backup prune", r);
    PQclear(r);
}

static PGresult *query(PGconn *c, char const *what, char const *sql, int n,
                       char const *const *params)
{
    PGresult *r = run(c, sql, n, params);
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
    {
        log_failure(what, r);
        PQclear(r);
        return NULL;
    }
    return r;
}

// appends every row of a one column result as a line; returns rows or -1
static long append_rows(struct sbuf *b, PGresult *r)
{
    int n = PQntuples(r);
    for (int i = 0; i < n; ++i)
        if (sb_line(b, PQgetvalue(r, i, 0)))
            return -1;
    return n;
}

static int pg_backup(struct ms_db *db, char const *since, char const *source,
                     char **out, size_t *out_len)
{
    struct pg *pg = db->ctx;
    int slot;
    PGconn *c = acquire(pg, &slot);
    struct sbuf b = {0};
    int rc = MS_DB_ERROR;
    int in_tx = 0;
    long count = 0;
    char src[64] = "", pruned[32] = "0", next[32] = "0";
    PGresult *r;
    cJSON *head = NULL;

    prune_changes(c);
    r = PQexec(c, "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    if (!is_command_ok(r))
    {
        log_failure("backup begin", r);
        PQclear(r);
        goto done;
    }
    PQclear(r);
    in_tx = 1;

    r = query(c, "backup meta",
              "SELECT (SELECT value FROM sync.meta WHERE key = 'source_id'),"
              " (SELECT value FROM sync.meta WHERE key = 'pruned_before'),"
              " pg_snapshot_xmin(pg_current_snapshot())::text",
              0, NULL);
    if (r == NULL)
        goto done;
    if (PQntuples(r) != 1 || PQgetisnull(r, 0, 0) || PQgetisnull(r, 0, 1))
        ms_log("db", "backup: sync.meta is incomplete");
    else
    {
        ms_strlcpy(src, PQgetvalue(r, 0, 0), sizeof src);
        ms_strlcpy(pruned, PQgetvalue(r, 0, 1), sizeof pruned);
        ms_strlcpy(next, PQgetvalue(r, 0, 2), sizeof next);
    }
    PQclear(r);
    if (src[0] == 0 || !digits_only(next) || !digits_only(pruned))
        goto done;

    int incremental = 0;
    if (digits_only(since) && source != NULL && strcmp(source, src) == 0)
    {
        char const *p[] = {since, pruned, next};
        r = query(c, "backup cursor",
                  "SELECT $1::bigint >= $2::bigint AND $1::bigint <= $3::bigint",
                  3, p);
        if (r == NULL)
            goto done;
        incremental = PQgetvalue(r, 0, 0)[0] == 't';
        PQclear(r);
    }

    head = cJSON_CreateObject();
    cJSON_AddStringToObject(head, "type", "head");
    cJSON_AddStringToObject(head, "source", src);
    cJSON_AddStringToObject(head, "node", MS_CONFIG.node_name);
    cJSON_AddStringToObject(head, "mode", incremental ? "incremental" : "snapshot");
    cJSON_AddStringToObject(head, "next", next);
    cJSON *list = cJSON_AddArrayToObject(head, "tables");

    if (incremental)
    {
        char const *p[] = {since, next};
        r = query(c, "backup changes",
                  "SELECT jsonb_build_object('t', tbl, 'op', op, 'row', data)::text"
                  " FROM sync.changes WHERE xid >= $1::bigint AND xid < $2::bigint"
                  " ORDER BY id",
                  2, p);
        if (r == NULL)
            goto done;
        // the head goes first, so collect the rows after it below
        char *head_text = cJSON_PrintUnformatted(head);
        int bad = head_text == NULL || sb_line(&b, head_text);
        free(head_text);
        count = bad ? -1 : append_rows(&b, r);
        PQclear(r);
        if (count < 0)
            goto done;
    }
    else
    {
        // tables with a capture trigger, parents before the tables that
        // reference them
        r = query(c, "backup tables",
                  "WITH RECURSIVE t AS (SELECT c.oid FROM pg_trigger g"
                  "  JOIN pg_class c ON c.oid = g.tgrelid"
                  "  WHERE g.tgname = 'sync_capture'),"
                  " d (rel, depth) AS (SELECT oid, 0 FROM t UNION ALL"
                  "  SELECT f.conrelid, d.depth + 1 FROM d"
                  "  JOIN pg_constraint f ON f.contype = 'f'"
                  "  AND f.confrelid = d.rel AND f.conrelid <> f.confrelid"
                  "  WHERE d.depth < 8 AND f.conrelid IN (SELECT oid FROM t))"
                  " SELECT n.nspname, c.relname FROM d"
                  " JOIN pg_class c ON c.oid = d.rel"
                  " JOIN pg_namespace n ON n.oid = c.relnamespace"
                  " GROUP BY n.nspname, c.relname ORDER BY max(d.depth), 1, 2",
                  0, NULL);
        if (r == NULL)
            goto done;
        int ntables = PQntuples(r);
        char (*names)[2][64] = calloc((size_t)ntables + 1, sizeof *names);
        int usable = 0;
        for (int i = 0; i < ntables; ++i)
        {
            if (!ident_ok(PQgetvalue(r, i, 0)) || !ident_ok(PQgetvalue(r, i, 1)))
            {
                ms_log("db", "backup: skipping table with an odd name");
                continue;
            }
            ms_strlcpy(names[usable][0], PQgetvalue(r, i, 0), 64);
            ms_strlcpy(names[usable][1], PQgetvalue(r, i, 1), 64);
            char full[130];
            snprintf(full, sizeof full, "%s.%s", names[usable][0], names[usable][1]);
            cJSON_AddItemToArray(list, cJSON_CreateString(full));
            ++usable;
        }
        PQclear(r);
        char *head_text = cJSON_PrintUnformatted(head);
        int bad = usable == 0 || head_text == NULL || sb_line(&b, head_text);
        free(head_text);
        for (int i = 0; i < usable && !bad; ++i)
        {
            char sql[400];
            snprintf(sql, sizeof sql,
                     "SELECT jsonb_build_object('t', '%s.%s', 'op', 'U',"
                     " 'row', to_jsonb(x))::text FROM \"%s\".\"%s\" x",
                     names[i][0], names[i][1], names[i][0], names[i][1]);
            r = query(c, "backup snapshot", sql, 0, NULL);
            long n = r ? append_rows(&b, r) : -1;
            PQclear(r);
            if (n < 0)
                bad = 1;
            else
                count += n;
        }
        free(names);
        if (bad)
            goto done;
    }

    char end[64];
    snprintf(end, sizeof end, "{\"type\":\"end\",\"count\":%ld}", count);
    if (sb_line(&b, end))
        goto done;
    *out = b.p;
    *out_len = b.len;
    b.p = NULL;
    rc = MS_DB_OK;

done:
    if (in_tx)
    {
        r = PQexec(c, "ROLLBACK");
        PQclear(r);
    }
    cJSON_Delete(head);
    free(b.p);
    release(pg, slot);
    return rc;
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
    db->link_put = pg_link_put;
    db->link_delete = pg_link_delete;
    db->links_get = pg_links_get;
    db->rysteria_get = pg_rysteria_get;
    db->rysteria_put = pg_rysteria_put;
    db->gardn_list = pg_gardn_list;
    db->gardn_apply = pg_gardn_apply;
    db->lb_submit = pg_lb_submit;
    db->lb_top = pg_lb_top;
    db->backup_pull = pg_backup;
    db->ping = pg_ping;
    db->close = pg_close;
    ms_log("db", "postgres ready (%d connections)", count);
    return db;
}

#endif // MS_WITH_POSTGRES
