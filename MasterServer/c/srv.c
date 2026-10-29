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
#include <stdlib.h>
#include <string.h>

#include "api.h"
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

// Applies one {uuid, add:[...], remove:[...]} entry; returns 0 on success
static int apply_change(cJSON const *change)
{
    char const *uuid = ms_json_str(change, "uuid");
    if (!ms_uuid_valid(uuid))
        return -1;
    cJSON const *add = cJSON_GetObjectItemCaseSensitive(change, "add");
    cJSON const *remove = cJSON_GetObjectItemCaseSensitive(change, "remove");
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
