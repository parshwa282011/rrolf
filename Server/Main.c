// Copyright (C) 2024 Paul Johnson
// Copyright (C) 2024-2025 Maxim Nesterov

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

#include <assert.h>
#include <fenv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef RIVET_BUILD
#include <curl/curl.h>
#endif

#include <Server/Logs.h>
#include <Server/Server.h>
#include <Shared/Api.h>
#include <Shared/MagicNumber.h>
#include <Shared/Rivet.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>

// Reads KEY=VALUE lines of ./.env into the environment without overriding
// variables that are already set (RR_BIOME, RR_PORT, RR_API_HOST,
// SERVER_SECRET, ...). The deployment scripts put one next to the binary.
#ifdef _WIN32
#define setenv(k, v, o) _putenv_s(k, v)
#endif

static void load_dotenv(void)
{
    FILE *f = fopen(".env", "r");
    if (f == NULL)
        return;
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || eq == NULL)
            continue;
        *eq = 0;
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = 0;
        size_t len = strlen(value);
        if (len >= 2 && (value[0] == '"' || value[0] == '\'') &&
            value[len - 1] == value[0])
        {
            value[len - 1] = 0;
            ++value;
        }
        if (line[0])
            setenv(line, value, 0);
    }
    fclose(f);
}

int main()
{
    load_dotenv();
    fprintf(stderr, "gameserver on version %llu\n", RR_SECRET8 ^ 255);
    srand(time(0));
    // signal(SIGINT, sigint_handle);
#ifdef RIVET_BUILD
    curl_global_init(CURL_GLOBAL_ALL);
#endif
    char startup_message[1000] = {0};
#ifdef NDEBUG
    strcat(startup_message, "release");
#else
    strcat(startup_message, "debug");
#endif
    strcat(startup_message, " build running on ");
#ifdef RIVET_BUILD
    char *version_name = getenv("RIVET_VERSION_NAME");
    char *lobby_id = getenv("RIVET_LOBBY_ID");
    assert(version_name && lobby_id);
    strcat(startup_message, "rivet\nversion: `");
    strcat(startup_message, version_name);
    strcat(startup_message, "`\nlobby id: `");
    strcat(startup_message, lobby_id);
    strcat(startup_message, "`");
#elif RR_WINDOWS
    strcat(startup_message, " windows");
#else
    strcat(startup_message, " linux");
#endif
#ifdef RIVET_BUILD
    rr_discord_webhook_log("server status", "server startup", startup_message,
                           0x4000ff);
    char const *lobby_token = getenv("RIVET_LOBBY_TOKEN");
    if (!lobby_token)
    {
        fputs("using rivet build without rivet lobby token env var set\n",
              stderr);
        abort();
    }
    rr_rivet_lobbies_ready(lobby_token);
#else

#endif
    struct rr_server *s = calloc(1, sizeof *s);
    rr_server_init(s);
    rr_server_run(s);
    rr_server_free(s);
}
