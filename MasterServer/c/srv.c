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

// Server-to-server routes (X-Server-Secret, checked by ms_route): used by the
// gardn server for token checks and batched progress saves, and by anything
// that needs leaderboard access.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "leaderboard.h"
#include "util.h"

#define MAX_FLUSH_CHANGES 512
#define MAX_LOADOUT_LEN 64

void ms_h_srv_verify(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r = ms_auth_verify(ms_json_str(body, "uuid"), NULL,
                                           ms_json_str(body, "pl_p"), &a);
    if (r != ms_auth_ok)
        ms_auth_res_failure(res, r);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "uuid", a.uuid);
        cJSON_AddStringToObject(o, "username", a.username);
        cJSON_AddBoolToObject(o, "is_dev", a.is_dev);
        ms_res_json(res, 200, o);
    }
    cJSON_Delete(body);
}

void ms_h_srv_gardn_load(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    struct ms_db_account a;
    enum ms_auth_result r = ms_auth_verify(ms_json_str(body, "uuid"), NULL,
                                           ms_json_str(body, "pl_p"), &a);
    if (r != ms_auth_ok)
    {
        ms_auth_res_failure(res, r);
        cJSON_Delete(body);
        return;
    }
    struct ms_db_gsave *saves = calloc(MS_GARDN_MAX_SAVES, sizeof *saves);
    int n = MS_DB->gardn_list(MS_DB, a.uuid, saves, MS_GARDN_MAX_SAVES);
    if (n < 0)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "username", a.username);
        cJSON_AddBoolToObject(o, "is_dev", a.is_dev);
        cJSON *arr = cJSON_AddArrayToObject(o, "saves");
        for (int i = 0; i < n; ++i)
        {
            cJSON *s = cJSON_CreateObject();
            cJSON_AddStringToObject(s, "save_id", saves[i].save_id);
            cJSON_AddNumberToObject(s, "score", saves[i].score);
            cJSON_AddNumberToObject(s, "created_at", (double)saves[i].created_at);
            cJSON *loadout = cJSON_Parse(saves[i].loadout);
            cJSON_AddItemToObject(s, "loadout",
                                  loadout ? loadout : cJSON_CreateArray());
            cJSON_AddItemToArray(arr, s);
        }
        ms_res_json(res, 200, o);
    }
    if (n > 0)
        ms_db_gsave_free(saves, n);
    free(saves);
    cJSON_Delete(body);
}

static int valid_save_id(char const *s)
{
    if (s == NULL)
        return 0;
    size_t len = strlen(s);
    if (len == 0 || len > 39)
        return 0;
    for (size_t i = 0; i < len; ++i)
    {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

// What the player's saves add up to now, read from the database (gardn keeps
// no list of them): the Mythic petals in all of them and the best score.
// `ids` are the petal ids gardn calls Mythic. 0 on success
static int account_stats(char const *uuid, cJSON const *ids, uint64_t *mythics,
                         uint64_t *best)
{
    struct ms_db_gsave *saves = calloc(MS_GARDN_MAX_SAVES, sizeof *saves);
    int n = MS_DB->gardn_list(MS_DB, uuid, saves, MS_GARDN_MAX_SAVES);
    *mythics = 0;
    *best = 0;
    for (int i = 0; i < n; ++i)
    {
        if (saves[i].score > *best)
            *best = saves[i].score;
        cJSON *loadout = cJSON_Parse(saves[i].loadout);
        cJSON const *slot;
        cJSON_ArrayForEach(slot, loadout)
        {
            cJSON const *id;
            if (!cJSON_IsNumber(slot))
                continue;
            cJSON_ArrayForEach(id, ids)
                if (cJSON_IsNumber(id) && id->valuedouble == slot->valuedouble)
                {
                    ++*mythics;
                    break;
                }
        }
        cJSON_Delete(loadout);
    }
    if (n > 0)
        ms_db_gsave_free(saves, n);
    free(saves);
    return n < 0 ? -1 : 0;
}

// Applies one {uuid, add:[...], remove:[...]} entry; returns 0 on success
static int apply_change(cJSON const *change)
{
    char const *uuid = ms_json_str(change, "uuid");
    if (!ms_uuid_valid(uuid))
        return -1;
    cJSON const *add = cJSON_GetObjectItemCaseSensitive(change, "add");
    cJSON const *remove = cJSON_GetObjectItemCaseSensitive(change, "remove");
    // an account that was deleted while its player was still in gardn: nothing is written
    struct ms_db_account existing;
    if (MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &existing) != MS_DB_OK)
        return 0;
    int add_count = cJSON_IsArray(add) ? cJSON_GetArraySize(add) : 0;
    int remove_count = cJSON_IsArray(remove) ? cJSON_GetArraySize(remove) : 0;
    if (add_count > MS_GARDN_MAX_SAVES || remove_count > MS_GARDN_MAX_SAVES * 2)
        return -1;

    struct ms_db_gsave *adds = calloc((size_t)add_count + 1, sizeof *adds);
    char(*removes)[40] = calloc((size_t)remove_count + 1, sizeof *removes);
    int ok = 1;
    int i = 0;
    cJSON const *item;
    cJSON_ArrayForEach(item, add)
    {
        char const *id = ms_json_str(item, "save_id");
        cJSON const *score = cJSON_GetObjectItemCaseSensitive(item, "score");
        cJSON const *loadout = cJSON_GetObjectItemCaseSensitive(item, "loadout");
        if (!valid_save_id(id) || !cJSON_IsNumber(score) ||
            score->valuedouble < 0 || score->valuedouble > 4294967295.0 ||
            !cJSON_IsArray(loadout) ||
            cJSON_GetArraySize(loadout) > MAX_LOADOUT_LEN)
        {
            ok = 0;
            break;
        }
        cJSON const *slot;
        cJSON_ArrayForEach(slot, loadout)
        {
            if (!cJSON_IsNumber(slot) || slot->valuedouble < 0 ||
                slot->valuedouble > 65535 ||
                slot->valuedouble != floor(slot->valuedouble))
                ok = 0;
        }
        if (!ok)
            break;
        ms_strlcpy(adds[i].save_id, id, sizeof adds[i].save_id);
        adds[i].score = (uint32_t)score->valuedouble;
        adds[i].loadout = cJSON_PrintUnformatted(loadout);
        cJSON const *created = cJSON_GetObjectItemCaseSensitive(item, "created_at");
        adds[i].created_at = cJSON_IsNumber(created) ? (int64_t)created->valuedouble
                                                     : 0;
        ++i;
    }
    add_count = i;
    int r = 0;
    cJSON_ArrayForEach(item, remove)
    {
        if (!cJSON_IsString(item) || !valid_save_id(item->valuestring))
        {
            ok = 0;
            break;
        }
        ms_strlcpy(removes[r++], item->valuestring, 40);
    }
    int result = -1;
    if (ok)
        result = MS_DB->gardn_apply(MS_DB, uuid, adds, add_count,
                                    (char const(*)[40])removes, r) == MS_DB_OK
                     ? 0
                     : -1;
    ms_db_gsave_free(adds, add_count);
    free(adds);
    free(removes);
    // what gardn counted for the leaderboards: kills since its last report, and
    // the Mythic petals and best score of the player's saves
    if (result == 0)
    {
        cJSON const *kills = cJSON_GetObjectItemCaseSensitive(change, "kills");
        cJSON const *mythics = cJSON_GetObjectItemCaseSensitive(change, "mythics");
        cJSON const *best = cJSON_GetObjectItemCaseSensitive(change, "best_score");
        uint64_t k = cJSON_IsNumber(kills) && kills->valuedouble > 0 &&
                             kills->valuedouble < 1e9
                         ? (uint64_t)kills->valuedouble
                         : 0;
        int have = cJSON_IsNumber(mythics) && cJSON_IsNumber(best) &&
                   mythics->valuedouble >= 0 && best->valuedouble >= 0 &&
                   mythics->valuedouble < 1e9 && best->valuedouble < 4294967296.0;
        uint64_t m = have ? (uint64_t)mythics->valuedouble : 0;
        uint64_t b = have ? (uint64_t)best->valuedouble : 0;
        // gardn sends the Mythic petal ids instead of keeping all the saves in
        // memory to add them up: the totals come from the database
        cJSON const *ids = cJSON_GetObjectItemCaseSensitive(change, "mythic_ids");
        if ((add_count > 0 || remove_count > 0) && cJSON_IsArray(ids) &&
            account_stats(uuid, ids, &m, &b) == 0)
            have = 1;
        if (k > 0 || have)
            ms_lb_push_gardn(uuid, k, have, m, b);
    }
    return result;
}

// Idempotent: the gardn server can resend a batch after a failure.
void ms_h_srv_gardn_flush(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    cJSON const *changes =
        body ? cJSON_GetObjectItemCaseSensitive(body, "changes") : NULL;
    if (!cJSON_IsArray(changes) || cJSON_GetArraySize(changes) > MAX_FLUSH_CHANGES)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    cJSON *failed = cJSON_CreateArray();
    int applied = 0;
    cJSON const *change;
    cJSON_ArrayForEach(change, changes)
    {
        if (apply_change(change) == 0)
            ++applied;
        else
        {
            char const *uuid = ms_json_str(change, "uuid");
            cJSON_AddItemToArray(failed, cJSON_CreateString(uuid ? uuid : ""));
        }
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", cJSON_GetArraySize(failed) == 0);
    cJSON_AddNumberToObject(o, "applied", applied);
    cJSON_AddItemToObject(o, "failed", failed);
    ms_res_json(res, 200, o);
    cJSON_Delete(body);
}

void ms_h_srv_lb_submit(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *board = ms_json_str(body, "board");
    char const *uuid = ms_json_str(body, "uuid");
    cJSON const *score = body ? cJSON_GetObjectItemCaseSensitive(body, "score")
                              : NULL;
    cJSON const *extra = body ? cJSON_GetObjectItemCaseSensitive(body, "extra")
                              : NULL;
    if (board == NULL || strlen(board) == 0 || strlen(board) > 63 ||
        !ms_uuid_valid(uuid) || !cJSON_IsNumber(score) ||
        !isfinite(score->valuedouble))
        ms_res_error(res, 400, "bad_request", NULL);
    else
    {
        char *extra_text = cJSON_IsObject(extra)
                               ? cJSON_PrintUnformatted(extra)
                               : strdup("{}");
        int rc = MS_DB->lb_submit(MS_DB, board, uuid, score->valuedouble,
                                  extra_text);
        free(extra_text);
        if (rc != MS_DB_OK)
            ms_res_error(res, 500, "server_error", NULL);
        else
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            ms_res_json(res, 200, o);
        }
    }
    cJSON_Delete(body);
}

void ms_h_srv_lb_top(struct ms_req *req, struct ms_res *res)
{
    char board[64], limit_text[16];
    if (!ms_query_arg(req->query, "board", board, sizeof board) ||
        board[0] == 0)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        return;
    }
    int limit = 10;
    if (ms_query_arg(req->query, "limit", limit_text, sizeof limit_text))
        limit = atoi(limit_text);
    if (limit < 1)
        limit = 1;
    if (limit > 100)
        limit = 100;
    struct ms_db_lb_entry *entries = calloc((size_t)limit, sizeof *entries);
    int n = MS_DB->lb_top(MS_DB, board, limit, entries);
    if (n < 0)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON *arr = cJSON_AddArrayToObject(o, "entries");
        for (int i = 0; i < n; ++i)
        {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "uuid", entries[i].uuid);
            cJSON_AddNumberToObject(e, "score", entries[i].score);
            cJSON *extra = cJSON_Parse(entries[i].extra);
            cJSON_AddItemToObject(e, "extra",
                                  extra ? extra : cJSON_CreateObject());
            cJSON_AddItemToArray(arr, e);
        }
        ms_res_json(res, 200, o);
    }
    if (n > 0)
        ms_db_lb_free(entries, n);
    free(entries);
}

// gardn restores a save: it is removed and handed back in one step, so that
// it can only ever be restored once. { uuid, save_id, mythic_ids }
void ms_h_srv_gardn_take(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    char const *uuid = ms_json_str(body, "uuid");
    char const *save_id = ms_json_str(body, "save_id");
    if (!ms_uuid_valid(uuid) || !valid_save_id(save_id))
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    struct ms_db_gsave save;
    struct ms_db_account existing;
    int rc = MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &existing) == MS_DB_OK
                 ? MS_DB->gardn_take(MS_DB, uuid, save_id, &save)
                 : MS_DB_NOT_FOUND;
    if (rc == MS_DB_NOT_FOUND)
        ms_res_error(res, 404, "not_found", "That save is gone");
    else if (rc != MS_DB_OK)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON *s = cJSON_AddObjectToObject(o, "save");
        cJSON_AddStringToObject(s, "save_id", save.save_id);
        cJSON_AddNumberToObject(s, "score", save.score);
        cJSON_AddNumberToObject(s, "created_at", (double)save.created_at);
        cJSON *loadout = cJSON_Parse(save.loadout);
        cJSON_AddItemToObject(s, "loadout", loadout ? loadout : cJSON_CreateArray());
        ms_db_gsave_free(&save, 1);
        ms_res_json(res, 200, o);
        // the Mythic petals of the saves that are left
        cJSON const *ids = cJSON_GetObjectItemCaseSensitive(body, "mythic_ids");
        uint64_t m, b;
        if (cJSON_IsArray(ids) && account_stats(uuid, ids, &m, &b) == 0)
            ms_lb_push_gardn(uuid, 0, 1, m, b);
    }
    cJSON_Delete(body);
}

// ---------------------------------------------------------- crash backups

#define MAX_BACKUP_PLAYERS 2000
#define MAX_RESTORED 512

// The gardn server reports every signed-in player it has, every 30 seconds:
// { players: [{ uuid, score, loadout: [petal ids] }] }. The table then holds
// exactly these players, so whoever left, or is not reported, is removed.
void ms_h_srv_gardn_backup(struct ms_req *req, struct ms_res *res)
{
    cJSON *body = ms_req_json(req);
    cJSON const *players =
        body ? cJSON_GetObjectItemCaseSensitive(body, "players") : NULL;
    if (!cJSON_IsArray(players) ||
        cJSON_GetArraySize(players) > MAX_BACKUP_PLAYERS)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        cJSON_Delete(body);
        return;
    }
    if (MS_DB->gardn_backup_replace == NULL)
    {
        ms_res_error(res, 501, "not_supported",
                     "This database keeps no crash backups");
        cJSON_Delete(body);
        return;
    }
    int count = cJSON_GetArraySize(players);
    struct ms_db_backup *list = calloc((size_t)count + 1, sizeof *list);
    int n = 0;
    int ok = 1;
    cJSON const *item;
    cJSON_ArrayForEach(item, players)
    {
        char const *uuid = ms_json_str(item, "uuid");
        cJSON const *score = cJSON_GetObjectItemCaseSensitive(item, "score");
        cJSON const *loadout = cJSON_GetObjectItemCaseSensitive(item, "loadout");
        if (!ms_uuid_valid(uuid) || !cJSON_IsNumber(score) ||
            score->valuedouble < 0 || score->valuedouble > 4294967295.0 ||
            !cJSON_IsArray(loadout) ||
            cJSON_GetArraySize(loadout) > MAX_LOADOUT_LEN)
        {
            ok = 0;
            break;
        }
        cJSON const *slot;
        cJSON_ArrayForEach(slot, loadout)
        {
            if (!cJSON_IsNumber(slot) || slot->valuedouble < 0 ||
                slot->valuedouble > 65535 ||
                slot->valuedouble != floor(slot->valuedouble))
                ok = 0;
        }
        if (!ok)
            break;
        ms_strlcpy(list[n].uuid, uuid, sizeof list[n].uuid);
        list[n].score = (uint32_t)score->valuedouble;
        list[n].loadout = cJSON_PrintUnformatted(loadout);
        ++n;
    }
    int rc = ok ? MS_DB->gardn_backup_replace(MS_DB, list, n) : MS_DB_ERROR;
    for (int i = 0; i < n; ++i)
        free(list[i].loadout);
    free(list);
    if (!ok)
        ms_res_error(res, 400, "bad_request", NULL);
    else if (rc != MS_DB_OK)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddNumberToObject(o, "count", n);
        ms_res_json(res, 200, o);
    }
    cJSON_Delete(body);
}

int ms_gardn_backup_restore()
{
    if (MS_DB == NULL || MS_DB->gardn_backup_restore == NULL)
        return -1;
    struct ms_db_backup *moved = calloc(MAX_RESTORED, sizeof *moved);
    int n = MS_DB->gardn_backup_restore(MS_DB, moved, MAX_RESTORED);
    if (n < 0)
        ms_log("backup", "could not turn the crash backups into saves");
    else
    {
        for (int i = 0; i < n && i < MAX_RESTORED; ++i)
            ms_lb_push_gardn_best(moved[i].uuid, moved[i].score);
        if (n > 0)
            ms_log("backup", "%d crash backup(s) are saves now", n);
    }
    free(moved);
    return n;
}

// Every start: what the gardn server last reported (its players when it was
// stopped or died) becomes saves before anything is served. A standby does not:
// its backups are the primary's, kept for the day it takes over
void ms_gardn_backup_restore_at_start()
{
    if (MS_CONFIG.sync_role_file[0] != 0)
    {
        FILE *f = fopen(MS_CONFIG.sync_role_file, "r");
        if (f != NULL)
        {
            char role[32] = "";
            if (fgets(role, sizeof role, f) == NULL)
                role[0] = 0;
            fclose(f);
            if (strncmp(role, "standby", 7) == 0)
            {
                ms_log("backup", "standby: the crash backups are kept for a takeover");
                return;
            }
        }
    }
    ms_gardn_backup_restore();
}

// syncd calls this on the machine that takes over, before its tunnel starts
void ms_h_srv_gardn_backup_restore(struct ms_req *req, struct ms_res *res)
{
    (void)req;
    int n = ms_gardn_backup_restore();
    if (n < 0)
        ms_res_error(res, 500, "server_error", NULL);
    else
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddNumberToObject(o, "moved", n);
        ms_res_json(res, 200, o);
    }
}
