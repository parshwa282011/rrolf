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

#include "db.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"

struct ms_db *ms_db_open()
{
    switch (MS_CONFIG.db_type)
    {
    case ms_db_type_postgres:
#ifdef MS_WITH_POSTGRES
        return ms_db_postgres_open();
#else
        ms_log("db", "this build has no PostgreSQL support");
        return NULL;
#endif
    case ms_db_type_mongo:
#ifdef MS_WITH_MONGO
        return ms_db_mongo_open();
#else
        ms_log("db", "this build has no MongoDB support");
        return NULL;
#endif
    default:
        ms_log("db", "unknown DB_TYPE");
        return NULL;
    }
}

void ms_db_close(struct ms_db *db)
{
    if (db == NULL)
        return;
    db->close(db);
}

void ms_db_rysteria_init(struct ms_db_rysteria *r)
{
    memset(r, 0, sizeof *r);
    r->petals = strdup("{}");
    r->failed_crafts = strdup("{}");
    r->mob_kills = strdup("{}");
    r->loadout = strdup("{}");
}

void ms_db_rysteria_free(struct ms_db_rysteria *r)
{
    free(r->petals);
    free(r->failed_crafts);
    free(r->mob_kills);
    free(r->loadout);
    r->petals = r->failed_crafts = r->mob_kills = r->loadout = NULL;
}

void ms_db_gsave_free(struct ms_db_gsave *saves, int count)
{
    for (int i = 0; i < count; ++i)
    {
        free(saves[i].loadout);
        saves[i].loadout = NULL;
    }
}

void ms_db_lb_free(struct ms_db_lb_entry *entries, int count)
{
    for (int i = 0; i < count; ++i)
    {
        free(entries[i].extra);
        entries[i].extra = NULL;
    }
}
