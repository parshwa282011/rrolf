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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "util.h"

// GET /backup?since=<cursor>&source=<id>&node=<name>
// Answers a backup server (X-Clone-Secret, checked by ms_route). Without a
// cursor, or when the cursor belongs to another source or is older than the
// kept history, the answer is a full snapshot; otherwise only the row changes
// since the cursor. The document format is described in db_postgres.c.
void ms_h_backup(struct ms_req *req, struct ms_res *res)
{
    if (MS_DB == NULL || MS_DB->backup_pull == NULL)
    {
        ms_res_error(res, 501, "not_supported",
                     "this database has no backup feed");
        return;
    }
    char since[32], source[64], node[48];
    ms_query_arg(req->query, "since", since, sizeof since);
    ms_query_arg(req->query, "source", source, sizeof source);
    ms_query_arg(req->query, "node", node, sizeof node);
    for (char *c = node; *c; ++c)
        if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_' && *c != '.')
            *c = '?';

    char *out = NULL;
    size_t len = 0;
    if (MS_DB->backup_pull(MS_DB, since[0] ? since : NULL,
                           source[0] ? source : NULL, &out, &len) != MS_DB_OK)
    {
        ms_res_error(res, 500, "backup_failed", NULL);
        return;
    }
    ms_res_free(res);
    res->status = 200;
    res->body = out;
    res->body_len = len;
    ms_strlcpy(res->content_type, "application/x-ndjson",
               sizeof res->content_type);
    ms_log("backup", "%s pulled %zu bytes (%s)", node[0] ? node : "?", len,
           since[0] ? "since cursor" : "first pull");
}
