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

#ifdef MS_WITH_MONGO

#include <mongoc/mongoc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "db.h"

// One collection per section in the configured database:
//   accounts       _id = uuid           (profile, credentials, flags)
//   account_links  _id = "provider:id"  (unique per provider account)
//   rysteria       _id = uuid
//   gardn          _id = "uuid:save_id" (one document per progress save)
//   leaderboards   _id = "board:uuid"

#define DUPLICATE_KEY 11000

struct mg
{
    mongoc_client_pool_t *pool;
    mongoc_uri_t *uri;
    char name[64];
};

static mongoc_collection_t *coll(struct mg *mg, mongoc_client_t *client,
                                 char const *name)
{
    return mongoc_client_get_collection(client, mg->name, name);
}

static int log_error(char const *what, bson_error_t const *error)
{
    ms_log("db", "mongo %s failed: %s", what, error->message);
    return MS_DB_ERROR;
}

static char const *str_field(bson_t const *doc, char const *key,
                             char const *fallback)
{
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_UTF8(&it))
        return bson_iter_utf8(&it, NULL);
    return fallback;
}

static int64_t int_field(bson_t const *doc, char const *key)
{
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_NUMBER(&it))
        return bson_iter_as_int64(&it);
    return 0;
}

static double double_field(bson_t const *doc, char const *key)
{
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_NUMBER(&it))
        return bson_iter_as_double(&it);
    return 0;
}

static int bool_field(bson_t const *doc, char const *key)
{
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_BOOL(&it))
        return bson_iter_bool(&it);
    return 0;
}

// json object text -> embedded document
static int append_json_doc(bson_t *doc, char const *key, char const *json)
{
    bson_error_t error;
    bson_t *sub = bson_new_from_json((uint8_t const *)json, -1, &error);
    if (sub == NULL)
        return 0;
    BSON_APPEND_DOCUMENT(doc, key, sub);
    bson_destroy(sub);
    return 1;
}

// json array text -> embedded array (wrapped so it parses as a document)
static int append_json_array(bson_t *doc, char const *key, char const *json)
{
    size_t len = strlen(json) + 16;
    char *wrapped = malloc(len);
    snprintf(wrapped, len, "{\"v\":%s}", json);
    bson_error_t error;
    bson_t *tmp = bson_new_from_json((uint8_t const *)wrapped, -1, &error);
    free(wrapped);
    if (tmp == NULL)
        return 0;
    bson_iter_t it;
    int ok = bson_iter_init_find(&it, tmp, "v") && BSON_ITER_HOLDS_ARRAY(&it);
    if (ok)
        BSON_APPEND_ITER(doc, key, &it);
    bson_destroy(tmp);
    return ok;
}

// embedded document -> malloc'd json text
static char *json_of_field(bson_t const *doc, char const *key, int array)
{
    bson_iter_t it;
    uint32_t len;
    uint8_t const *data;
    if (bson_iter_init_find(&it, doc, key) &&
        (array ? BSON_ITER_HOLDS_ARRAY(&it) : BSON_ITER_HOLDS_DOCUMENT(&it)))
    {
        if (array)
            bson_iter_array(&it, &len, &data);
        else
            bson_iter_document(&it, &len, &data);
        bson_t sub;
        if (bson_init_static(&sub, data, len))
        {
            char *s = array ? bson_array_as_relaxed_extended_json(&sub, NULL)
                            : bson_as_relaxed_extended_json(&sub, NULL);
            if (s != NULL)
            {
                char *copy = strdup(s);
                bson_free(s);
                return copy;
            }
        }
    }
    return strdup(array ? "[]" : "{}");
}

static void read_account(bson_t const *doc, struct ms_db_account *out)
{
    memset(out, 0, sizeof *out);
    ms_strlcpy(out->uuid, str_field(doc, "_id", ""), sizeof out->uuid);
    ms_strlcpy(out->username, str_field(doc, "username", ""),
               sizeof out->username);
    ms_strlcpy(out->email, str_field(doc, "email", ""), sizeof out->email);
    out->email_verified = bool_field(doc, "email_verified");
    ms_strlcpy(out->pw_hash, str_field(doc, "pw_hash", ""),
               sizeof out->pw_hash);
    ms_hex_decode(out->token_key, sizeof out->token_key,
                  str_field(doc, "token_key", ""));
    out->is_dev = bool_field(doc, "is_dev");
    out->created_at = int_field(doc, "created_at");
    out->last_login_at = int_field(doc, "last_login_at");
}

// finds one document; returns MS_DB_OK / MS_DB_NOT_FOUND / MS_DB_ERROR and
// leaves a copy in *out (caller bson_destroy's it)
static int find_one(mongoc_collection_t *c, bson_t const *filter, bson_t **out)
{
    mongoc_cursor_t *cursor =
        mongoc_collection_find_with_opts(c, filter, NULL, NULL);
    bson_t const *doc;
    int result = MS_DB_NOT_FOUND;
    if (mongoc_cursor_next(cursor, &doc))
    {
        *out = bson_copy(doc);
        result = MS_DB_OK;
    }
    bson_error_t error;
    if (mongoc_cursor_error(cursor, &error))
        result = log_error("find", &error);
    mongoc_cursor_destroy(cursor);
    return result;
}

static int mg_account_create(struct ms_db *db, struct ms_db_account const *a)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "accounts");
    char lower_name[sizeof a->username];
    char lower_email[sizeof a->email];
    ms_str_lower(lower_name, a->username, sizeof lower_name);
    ms_str_lower(lower_email, a->email, sizeof lower_email);
    char key[65];
    ms_hex_encode(key, a->token_key, 32);

    bson_t *doc = bson_new();
    BSON_APPEND_UTF8(doc, "_id", a->uuid);
    BSON_APPEND_UTF8(doc, "username", a->username);
    BSON_APPEND_UTF8(doc, "username_lower", lower_name);
    BSON_APPEND_UTF8(doc, "email", a->email);
    BSON_APPEND_UTF8(doc, "email_lower", lower_email);
    BSON_APPEND_BOOL(doc, "email_verified", a->email_verified);
    BSON_APPEND_UTF8(doc, "pw_hash", a->pw_hash);
    BSON_APPEND_UTF8(doc, "token_key", key);
    BSON_APPEND_BOOL(doc, "is_dev", a->is_dev);
    BSON_APPEND_INT64(doc, "created_at", a->created_at);
    BSON_APPEND_INT64(doc, "last_login_at", a->last_login_at);

    bson_error_t error;
    int result;
    if (mongoc_collection_insert_one(c, doc, NULL, NULL, &error))
        result = MS_DB_OK;
    else if (error.code == DUPLICATE_KEY)
        result = strstr(error.message, "email") ? MS_DB_CONFLICT_EMAIL
                                                 : MS_DB_CONFLICT_USERNAME;
    else
        result = log_error("account_create", &error);
    bson_destroy(doc);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_account_get(struct ms_db *db, enum ms_db_account_key key,
                          char const *a, char const *b,
                          struct ms_db_account *out)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    int result = MS_DB_NOT_FOUND;
    bson_t *found = NULL;

    char uuid[MS_UUID_SIZE];
    ms_strlcpy(uuid, a, sizeof uuid);
    if (key == ms_db_by_link)
    {
        char link_id[128];
        snprintf(link_id, sizeof link_id, "%s:%s", a, b);
        mongoc_collection_t *lc = coll(mg, client, "account_links");
        bson_t *filter = BCON_NEW("_id", BCON_UTF8(link_id));
        bson_t *link = NULL;
        result = find_one(lc, filter, &link);
        if (result == MS_DB_OK)
        {
            ms_strlcpy(uuid, str_field(link, "uuid", ""), sizeof uuid);
            bson_destroy(link);
        }
        bson_destroy(filter);
        mongoc_collection_destroy(lc);
    }
    else
        result = MS_DB_OK;

    if (result == MS_DB_OK)
    {
        mongoc_collection_t *c = coll(mg, client, "accounts");
        bson_t *filter;
        switch (key)
        {
        case ms_db_by_username:
            filter = BCON_NEW("username_lower", BCON_UTF8(a));
            break;
        case ms_db_by_email:
            // accounts without an email are stored as "" and never match
            filter = BCON_NEW("email_lower", BCON_UTF8(a[0] ? a : "\x01"));
            break;
        default:
            filter = BCON_NEW("_id", BCON_UTF8(uuid));
            break;
        }
        result = find_one(c, filter, &found);
        if (result == MS_DB_OK)
            read_account(found, out);
        bson_destroy(filter);
        mongoc_collection_destroy(c);
    }
    if (found != NULL)
        bson_destroy(found);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_update_one(struct ms_db *db, char const *collection,
                         bson_t const *filter, bson_t const *update,
                         char const *what, int upsert)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, collection);
    bson_t *opts = upsert ? BCON_NEW("upsert", BCON_BOOL(true)) : NULL;
    bson_error_t error;
    int result = mongoc_collection_update_one(c, filter, update, opts, NULL,
                                              &error)
                     ? MS_DB_OK
                     : log_error(what, &error);
    if (opts != NULL)
        bson_destroy(opts);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_account_set_credentials(struct ms_db *db, char const *uuid,
                                      char const *pw_hash,
                                      uint8_t const token_key[32])
{
    char key[65];
    ms_hex_encode(key, token_key, 32);
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(uuid));
    bson_t *update = BCON_NEW("$set", "{", "pw_hash", BCON_UTF8(pw_hash),
                              "token_key", BCON_UTF8(key), "}");
    int r = mg_update_one(db, "accounts", filter, update, "set_credentials", 0);
    bson_destroy(filter);
    bson_destroy(update);
    return r;
}

static int mg_account_set_last_login(struct ms_db *db, char const *uuid,
                                     int64_t when)
{
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(uuid));
    bson_t *update =
        BCON_NEW("$set", "{", "last_login_at", BCON_INT64(when), "}");
    int r = mg_update_one(db, "accounts", filter, update, "set_last_login", 0);
    bson_destroy(filter);
    bson_destroy(update);
    return r;
}

static int mg_link_put(struct ms_db *db, char const *uuid,
                       struct ms_db_link const *l)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "account_links");
    char link_id[128];
    snprintf(link_id, sizeof link_id, "%s:%s", l->provider, l->provider_user_id);

    int result = MS_DB_OK;
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(link_id));
    bson_t *existing = NULL;
    int found = find_one(c, filter, &existing);
    if (found == MS_DB_OK)
    {
        if (strcmp(str_field(existing, "uuid", ""), uuid) != 0)
            result = MS_DB_CONFLICT_LINK;
        bson_destroy(existing);
    }
    else if (found == MS_DB_ERROR)
        result = MS_DB_ERROR;
    bson_destroy(filter);

    bson_error_t error;
    if (result == MS_DB_OK)
    {
        // replacing the same provider with a different provider account
        bson_t *old = BCON_NEW("uuid", BCON_UTF8(uuid), "provider",
                               BCON_UTF8(l->provider), "_id", "{", "$ne",
                               BCON_UTF8(link_id), "}");
        mongoc_collection_delete_many(c, old, NULL, NULL, &error);
        bson_destroy(old);

        bson_t *doc = bson_new();
        BSON_APPEND_UTF8(doc, "_id", link_id);
        BSON_APPEND_UTF8(doc, "uuid", uuid);
        BSON_APPEND_UTF8(doc, "provider", l->provider);
        BSON_APPEND_UTF8(doc, "provider_user_id", l->provider_user_id);
        BSON_APPEND_UTF8(doc, "provider_email", l->provider_email);
        BSON_APPEND_UTF8(doc, "display_name", l->display_name);
        BSON_APPEND_INT64(doc, "linked_at", l->linked_at);
        bson_t *sel = BCON_NEW("_id", BCON_UTF8(link_id));
        bson_t *opts = BCON_NEW("upsert", BCON_BOOL(true));
        if (!mongoc_collection_replace_one(c, sel, doc, opts, NULL, &error))
            result = error.code == DUPLICATE_KEY ? MS_DB_CONFLICT_LINK
                                                 : log_error("link_put", &error);
        bson_destroy(sel);
        bson_destroy(opts);
        bson_destroy(doc);
    }
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_link_delete(struct ms_db *db, char const *uuid,
                          char const *provider)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "account_links");
    bson_t *filter =
        BCON_NEW("uuid", BCON_UTF8(uuid), "provider", BCON_UTF8(provider));
    bson_error_t error;
    int result = mongoc_collection_delete_many(c, filter, NULL, NULL, &error)
                     ? MS_DB_OK
                     : log_error("link_delete", &error);
    bson_destroy(filter);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_links_get(struct ms_db *db, char const *uuid,
                        struct ms_db_link *out, int max)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "account_links");
    bson_t *filter = BCON_NEW("uuid", BCON_UTF8(uuid));
    mongoc_cursor_t *cursor =
        mongoc_collection_find_with_opts(c, filter, NULL, NULL);
    bson_t const *doc;
    int n = 0;
    while (n < max && mongoc_cursor_next(cursor, &doc))
    {
        memset(&out[n], 0, sizeof out[n]);
        ms_strlcpy(out[n].provider, str_field(doc, "provider", ""),
                   sizeof out[n].provider);
        ms_strlcpy(out[n].provider_user_id, str_field(doc, "provider_user_id", ""),
                   sizeof out[n].provider_user_id);
        ms_strlcpy(out[n].provider_email, str_field(doc, "provider_email", ""),
                   sizeof out[n].provider_email);
        ms_strlcpy(out[n].display_name, str_field(doc, "display_name", ""),
                   sizeof out[n].display_name);
        out[n].linked_at = int_field(doc, "linked_at");
        ++n;
    }
    bson_error_t error;
    int result = mongoc_cursor_error(cursor, &error)
                     ? log_error("links_get", &error)
                     : n;
    mongoc_cursor_destroy(cursor);
    bson_destroy(filter);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_rysteria_get(struct ms_db *db, char const *uuid,
                           struct ms_db_rysteria *out)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "rysteria");
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(uuid));
    bson_t *doc = NULL;
    int result = find_one(c, filter, &doc);
    if (result == MS_DB_OK)
    {
        ms_db_rysteria_free(out);
        out->xp = double_field(doc, "xp");
        out->checkpoint = (int)int_field(doc, "checkpoint");
        out->petals = json_of_field(doc, "petals", 0);
        out->failed_crafts = json_of_field(doc, "failed_crafts", 0);
        out->mob_kills = json_of_field(doc, "mob_kills", 0);
        out->loadout = json_of_field(doc, "loadout", 0);
        ms_strlcpy(out->nickname, str_field(doc, "nickname", ""),
                   sizeof out->nickname);
        bson_destroy(doc);
    }
    bson_destroy(filter);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_rysteria_put(struct ms_db *db, char const *uuid,
                           struct ms_db_rysteria const *v)
{
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(uuid));
    bson_t set;
    bson_init(&set);
    BSON_APPEND_DOUBLE(&set, "xp", v->xp);
    BSON_APPEND_INT32(&set, "checkpoint", v->checkpoint);
    int ok = append_json_doc(&set, "petals", v->petals) &&
             append_json_doc(&set, "failed_crafts", v->failed_crafts) &&
             append_json_doc(&set, "mob_kills", v->mob_kills) &&
             append_json_doc(&set, "loadout", v->loadout);
    BSON_APPEND_UTF8(&set, "nickname", v->nickname);
    BSON_APPEND_INT64(&set, "updated_at", ms_now());
    int result;
    if (!ok)
    {
        ms_log("db", "mongo rysteria_put: invalid json in record");
        result = MS_DB_ERROR;
    }
    else
    {
        bson_t *update = bson_new();
        BSON_APPEND_DOCUMENT(update, "$set", &set);
        result = mg_update_one(db, "rysteria", filter, update, "rysteria_put", 1);
        bson_destroy(update);
    }
    bson_destroy(&set);
    bson_destroy(filter);
    return result;
}

static int mg_gardn_list(struct ms_db *db, char const *uuid,
                         struct ms_db_gsave *out, int max)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "gardn");
    bson_t *filter = BCON_NEW("uuid", BCON_UTF8(uuid));
    bson_t *opts = BCON_NEW("sort", "{", "created_at", BCON_INT32(-1),
                            "save_id", BCON_INT32(1), "}");
    mongoc_cursor_t *cursor =
        mongoc_collection_find_with_opts(c, filter, opts, NULL);
    bson_t const *doc;
    int n = 0;
    while (n < max && mongoc_cursor_next(cursor, &doc))
    {
        memset(&out[n], 0, sizeof out[n]);
        ms_strlcpy(out[n].save_id, str_field(doc, "save_id", ""),
                   sizeof out[n].save_id);
        out[n].score = (uint32_t)int_field(doc, "score");
        out[n].loadout = json_of_field(doc, "loadout", 1);
        out[n].created_at = int_field(doc, "created_at");
        ++n;
    }
    bson_error_t error;
    int result = mongoc_cursor_error(cursor, &error)
                     ? log_error("gardn_list", &error)
                     : n;
    mongoc_cursor_destroy(cursor);
    bson_destroy(opts);
    bson_destroy(filter);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_gardn_apply(struct ms_db *db, char const *uuid,
                          struct ms_db_gsave const *add, int add_count,
                          char const (*remove)[40], int remove_count)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "gardn");
    int result = MS_DB_OK;
    bson_error_t error;

    for (int i = 0; result == MS_DB_OK && i < add_count; ++i)
    {
        char id[128];
        snprintf(id, sizeof id, "%s:%s", uuid, add[i].save_id);
        bson_t set;
        bson_init(&set);
        BSON_APPEND_UTF8(&set, "uuid", uuid);
        BSON_APPEND_UTF8(&set, "save_id", add[i].save_id);
        BSON_APPEND_INT64(&set, "score", add[i].score);
        if (!append_json_array(&set, "loadout", add[i].loadout))
        {
            ms_log("db", "mongo gardn_apply: invalid loadout json");
            result = MS_DB_ERROR;
        }
        bson_t *filter = BCON_NEW("_id", BCON_UTF8(id));
        bson_t *update = bson_new();
        BSON_APPEND_DOCUMENT(update, "$set", &set);
        // created_at only set when the save is new
        bson_t *once = BCON_NEW(
            "created_at",
            BCON_INT64(add[i].created_at ? add[i].created_at : ms_now()));
        BSON_APPEND_DOCUMENT(update, "$setOnInsert", once);
        bson_t *opts = BCON_NEW("upsert", BCON_BOOL(true));
        if (result == MS_DB_OK &&
            !mongoc_collection_update_one(c, filter, update, opts, NULL, &error))
            result = log_error("gardn_apply upsert", &error);
        bson_destroy(opts);
        bson_destroy(once);
        bson_destroy(update);
        bson_destroy(filter);
        bson_destroy(&set);
    }
    for (int i = 0; result == MS_DB_OK && i < remove_count; ++i)
    {
        char id[128];
        snprintf(id, sizeof id, "%s:%s", uuid, remove[i]);
        bson_t *filter = BCON_NEW("_id", BCON_UTF8(id));
        if (!mongoc_collection_delete_one(c, filter, NULL, NULL, &error))
            result = log_error("gardn_apply delete", &error);
        bson_destroy(filter);
    }
    if (result == MS_DB_OK)
    {
        // trim to the newest MS_GARDN_MAX_SAVES
        bson_t *filter = BCON_NEW("uuid", BCON_UTF8(uuid));
        bson_t *opts = BCON_NEW("sort", "{", "created_at", BCON_INT32(-1),
                                "save_id", BCON_INT32(1), "}", "projection",
                                "{", "_id", BCON_INT32(1), "}", "skip",
                                BCON_INT32(MS_GARDN_MAX_SAVES));
        mongoc_cursor_t *cursor =
            mongoc_collection_find_with_opts(c, filter, opts, NULL);
        bson_t const *doc;
        while (mongoc_cursor_next(cursor, &doc))
        {
            bson_t *del = BCON_NEW("_id", BCON_UTF8(str_field(doc, "_id", "")));
            mongoc_collection_delete_one(c, del, NULL, NULL, &error);
            bson_destroy(del);
        }
        mongoc_cursor_destroy(cursor);
        bson_destroy(opts);
        bson_destroy(filter);
    }
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_lb_submit(struct ms_db *db, char const *board, char const *uuid,
                        double score, char const *extra_json)
{
    char id[160];
    snprintf(id, sizeof id, "%s:%s", board, uuid);
    bson_t *filter = BCON_NEW("_id", BCON_UTF8(id));
    bson_t set;
    bson_init(&set);
    BSON_APPEND_UTF8(&set, "board", board);
    BSON_APPEND_UTF8(&set, "uuid", uuid);
    BSON_APPEND_INT64(&set, "updated_at", ms_now());
    int ok = append_json_doc(&set, "extra", extra_json ? extra_json : "{}");
    int result = MS_DB_ERROR;
    if (ok)
    {
        bson_t *update = bson_new();
        BSON_APPEND_DOCUMENT(update, "$set", &set);
        bson_t *max = BCON_NEW("score", BCON_DOUBLE(score));
        BSON_APPEND_DOCUMENT(update, "$max", max);
        result = mg_update_one(db, "leaderboards", filter, update, "lb_submit", 1);
        bson_destroy(max);
        bson_destroy(update);
    }
    bson_destroy(&set);
    bson_destroy(filter);
    return result;
}

static int mg_lb_top(struct ms_db *db, char const *board, int limit,
                     struct ms_db_lb_entry *out)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    mongoc_collection_t *c = coll(mg, client, "leaderboards");
    bson_t *filter = BCON_NEW("board", BCON_UTF8(board));
    bson_t *opts = BCON_NEW("sort", "{", "score", BCON_INT32(-1), "}", "limit",
                            BCON_INT32(limit));
    mongoc_cursor_t *cursor =
        mongoc_collection_find_with_opts(c, filter, opts, NULL);
    bson_t const *doc;
    int n = 0;
    while (n < limit && mongoc_cursor_next(cursor, &doc))
    {
        memset(&out[n], 0, sizeof out[n]);
        ms_strlcpy(out[n].uuid, str_field(doc, "uuid", ""), sizeof out[n].uuid);
        out[n].score = double_field(doc, "score");
        out[n].extra = json_of_field(doc, "extra", 0);
        ++n;
    }
    bson_error_t error;
    int result = mongoc_cursor_error(cursor, &error)
                     ? log_error("lb_top", &error)
                     : n;
    mongoc_cursor_destroy(cursor);
    bson_destroy(opts);
    bson_destroy(filter);
    mongoc_collection_destroy(c);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static int mg_ping(struct ms_db *db)
{
    struct mg *mg = db->ctx;
    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    bson_t *cmd = BCON_NEW("ping", BCON_INT32(1));
    bson_error_t error;
    int result = mongoc_client_command_simple(client, "admin", cmd, NULL, NULL,
                                              &error)
                     ? MS_DB_OK
                     : log_error("ping", &error);
    bson_destroy(cmd);
    mongoc_client_pool_push(mg->pool, client);
    return result;
}

static void mg_close(struct ms_db *db)
{
    struct mg *mg = db->ctx;
    mongoc_client_pool_destroy(mg->pool);
    mongoc_uri_destroy(mg->uri);
    free(mg);
    free(db);
    mongoc_cleanup();
}

static int create_index(struct mg *mg, mongoc_client_t *client,
                        char const *collection, bson_t *spec)
{
    bson_t *cmd = BCON_NEW("createIndexes", BCON_UTF8(collection), "indexes",
                           "[", BCON_DOCUMENT(spec), "]");
    bson_error_t error;
    int ok = mongoc_client_command_simple(client, mg->name, cmd, NULL, NULL,
                                          &error);
    if (!ok)
        ms_log("db", "mongo createIndexes(%s) failed: %s", collection,
               error.message);
    bson_destroy(cmd);
    return ok;
}

struct ms_db *ms_db_mongo_open()
{
    mongoc_init();
    bson_error_t error;
    struct mg *mg = calloc(1, sizeof *mg);
    ms_strlcpy(mg->name, MS_CONFIG.mongo_db_name, sizeof mg->name);
    mg->uri = mongoc_uri_new_with_error(MS_CONFIG.mongo_uri, &error);
    if (mg->uri == NULL)
    {
        ms_log("db", "bad MONGO_URI: %s", error.message);
        free(mg);
        return NULL;
    }
    mongoc_uri_set_option_as_int32(mg->uri, "maxPoolSize",
                                   (int32_t)MS_CONFIG.db_pool_size);
    mg->pool = mongoc_client_pool_new(mg->uri);
    if (mg->pool == NULL)
    {
        ms_log("db", "could not create mongo client pool");
        mongoc_uri_destroy(mg->uri);
        free(mg);
        return NULL;
    }
    mongoc_client_pool_set_appname(mg->pool, "rysteria-master");

    mongoc_client_t *client = mongoc_client_pool_pop(mg->pool);
    int ok = 1;
    bson_t *spec;

    spec = BCON_NEW("key", "{", "username_lower", BCON_INT32(1), "}", "name",
                    BCON_UTF8("username_lower_unique"), "unique",
                    BCON_BOOL(true));
    ok &= create_index(mg, client, "accounts", spec);
    bson_destroy(spec);

    spec = BCON_NEW("key", "{", "email_lower", BCON_INT32(1), "}", "name",
                    BCON_UTF8("email_lower_unique"), "unique", BCON_BOOL(true),
                    "partialFilterExpression", "{", "email_lower", "{", "$gt",
                    BCON_UTF8(""), "}", "}");
    ok &= create_index(mg, client, "accounts", spec);
    bson_destroy(spec);

    spec = BCON_NEW("key", "{", "uuid", BCON_INT32(1), "provider", BCON_INT32(1),
                    "}", "name", BCON_UTF8("uuid_provider_unique"), "unique",
                    BCON_BOOL(true));
    ok &= create_index(mg, client, "account_links", spec);
    bson_destroy(spec);

    spec = BCON_NEW("key", "{", "uuid", BCON_INT32(1), "created_at",
                    BCON_INT32(-1), "}", "name", BCON_UTF8("uuid_created"));
    ok &= create_index(mg, client, "gardn", spec);
    bson_destroy(spec);

    spec = BCON_NEW("key", "{", "board", BCON_INT32(1), "score", BCON_INT32(-1),
                    "}", "name", BCON_UTF8("board_score"));
    ok &= create_index(mg, client, "leaderboards", spec);
    bson_destroy(spec);
    mongoc_client_pool_push(mg->pool, client);

    if (!ok)
    {
        mongoc_client_pool_destroy(mg->pool);
        mongoc_uri_destroy(mg->uri);
        free(mg);
        return NULL;
    }

    struct ms_db *db = calloc(1, sizeof *db);
    db->name = "mongo";
    db->ctx = mg;
    db->account_create = mg_account_create;
    db->account_get = mg_account_get;
    db->account_set_credentials = mg_account_set_credentials;
    db->account_set_last_login = mg_account_set_last_login;
    db->link_put = mg_link_put;
    db->link_delete = mg_link_delete;
    db->links_get = mg_links_get;
    db->rysteria_get = mg_rysteria_get;
    db->rysteria_put = mg_rysteria_put;
    db->gardn_list = mg_gardn_list;
    db->gardn_apply = mg_gardn_apply;
    db->lb_submit = mg_lb_submit;
    db->lb_top = mg_lb_top;
    db->ping = mg_ping;
    db->close = mg_close;
    ms_log("db", "mongo ready (database %s)", mg->name);
    return db;
}

#endif // MS_WITH_MONGO
