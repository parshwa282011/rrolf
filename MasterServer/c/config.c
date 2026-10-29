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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

struct ms_config MS_CONFIG;

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        ++start;
    if (start)
        memmove(s, s + start, strlen(s + start) + 1);
}

int ms_config_load(char const *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        ms_log("config", "no env file at %s, using process environment", path);
        return ms_config_from_env();
    }
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        trim(line);
        if (line[0] == 0 || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = 0;
        char *key = line;
        char *value = eq + 1;
        trim(key);
        trim(value);
        size_t len = strlen(value);
        if (len >= 2 && ((value[0] == '"' && value[len - 1] == '"') ||
                         (value[0] == '\'' && value[len - 1] == '\'')))
        {
            value[len - 1] = 0;
            ++value;
        }
        if (key[0] == 0)
            continue;
        setenv(key, value, 0);
    }
    fclose(f);
    return ms_config_from_env();
}

static void get_str(char *dst, size_t size, char const *name,
                    char const *fallback)
{
    char const *v = getenv(name);
    ms_strlcpy(dst, v != NULL ? v : fallback, size);
}

static uint32_t get_uint(char const *name, uint32_t fallback)
{
    char const *v = getenv(name);
    if (v == NULL || v[0] == 0)
        return fallback;
    char *end;
    unsigned long n = strtoul(v, &end, 10);
    if (*end != 0)
        return fallback;
    return (uint32_t)n;
}

int ms_config_from_env()
{
    struct ms_config *c = &MS_CONFIG;
    memset(c, 0, sizeof *c);

    char type[32];
    get_str(type, sizeof type, "DB_TYPE", "");
    ms_str_lower(type, type, sizeof type);
    if (strcmp(type, "postgres") == 0 || strcmp(type, "postgresql") == 0)
        c->db_type = ms_db_type_postgres;
    else if (strcmp(type, "mongo") == 0 || strcmp(type, "mongodb") == 0)
        c->db_type = ms_db_type_mongo;
    else
    {
        ms_log("config", "DB_TYPE must be 'postgres' or 'mongo'");
        return -1;
    }

    get_str(c->postgres_uri, sizeof c->postgres_uri, "POSTGRES_URI", "");
    get_str(c->mongo_uri, sizeof c->mongo_uri, "MONGO_URI", "");
    get_str(c->mongo_db_name, sizeof c->mongo_db_name, "MONGO_DB_NAME",
            "rysteria");
    c->port = (uint16_t)get_uint("PORT", 55554);
    get_str(c->bind_address, sizeof c->bind_address, "BIND_ADDRESS", "");
    get_str(c->allowed_origins, sizeof c->allowed_origins, "ALLOWED_ORIGINS",
            "");
    get_str(c->server_secret, sizeof c->server_secret, "SERVER_SECRET", "");
    get_str(c->token_secret_1, sizeof c->token_secret_1, "TOKEN_SECRET_1", "");
    get_str(c->token_secret_2, sizeof c->token_secret_2, "TOKEN_SECRET_2", "");
    get_str(c->otp_pepper, sizeof c->otp_pepper, "OTP_PEPPER", "");
    get_str(c->client_url, sizeof c->client_url, "CLIENT_URL", "");
    get_str(c->discord_client_id, sizeof c->discord_client_id,
            "DISCORD_CLIENT_ID", "");
    get_str(c->discord_client_secret, sizeof c->discord_client_secret,
            "DISCORD_CLIENT_SECRET", "");
    get_str(c->discord_bot_token, sizeof c->discord_bot_token,
            "DISCORD_BOT_TOKEN", "");
    get_str(c->discord_redirect_uri, sizeof c->discord_redirect_uri,
            "DISCORD_REDIRECT_URI", "");
    get_str(c->trust_proxy_header, sizeof c->trust_proxy_header,
            "TRUST_PROXY_HEADER", "");
    c->flush_interval_ms = get_uint("FLUSH_INTERVAL_MS", 5000);
    if (c->flush_interval_ms < 250)
        c->flush_interval_ms = 250;
    c->db_pool_size = get_uint("DB_POOL_SIZE", 4);
    if (c->db_pool_size < 1)
        c->db_pool_size = 1;
    if (c->db_pool_size > 32)
        c->db_pool_size = 32;
    c->pl_p_lifetime_seconds = get_uint("PL_P_LIFETIME_SECONDS", 7 * 24 * 3600);

    // secrets must exist and be long enough to be worth anything
    struct
    {
        char const *name;
        char const *value;
    } required[] = {
        {"SERVER_SECRET", c->server_secret},
        {"TOKEN_SECRET_1", c->token_secret_1},
        {"TOKEN_SECRET_2", c->token_secret_2},
        {"OTP_PEPPER", c->otp_pepper},
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof required / sizeof *required; ++i)
        if (strlen(required[i].value) < 16)
        {
            ms_log("config", "%s must be set to at least 16 characters",
                   required[i].name);
            ok = 0;
        }
    if (c->db_type == ms_db_type_postgres && c->postgres_uri[0] == 0)
    {
        ms_log("config", "POSTGRES_URI is required when DB_TYPE=postgres");
        ok = 0;
    }
    if (c->db_type == ms_db_type_mongo && c->mongo_uri[0] == 0)
    {
        ms_log("config", "MONGO_URI is required when DB_TYPE=mongo");
        ok = 0;
    }
    return ok ? 0 : -1;
}
