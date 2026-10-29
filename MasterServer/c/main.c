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

#include <signal.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "db.h"
#include "discord.h"
#include "jobs.h"
#include "server.h"
#include "util.h"

static void on_signal(int sig) { ms_server_stop(); }

int main(int argc, char **argv)
{
    char const *env_path = ".env";
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--env") == 0 && i + 1 < argc)
            env_path = argv[++i];
        else
        {
            fprintf(stderr, "usage: %s [--env <file>]\n", argv[0]);
            return 2;
        }
    }
    if (sodium_init() < 0)
    {
        fprintf(stderr, "libsodium failed to initialise\n");
        return 1;
    }
    if (ms_config_load(env_path) != 0)
        return 1;

    signal(SIGPIPE, SIG_IGN);
    ms_http_global_init();

    MS_DB = ms_db_open();
    if (MS_DB == NULL)
    {
        ms_http_global_cleanup();
        return 1;
    }
    int workers = (int)MS_CONFIG.db_pool_size;
    if (workers < 4)
        workers = 4;
    ms_jobs_init(workers, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int rc = ms_server_run();
    if (rc != 0)
        ms_jobs_shutdown();

    ms_db_close(MS_DB);
    MS_DB = NULL;
    ms_http_global_cleanup();
    ms_log("main", "bye");
    return rc == 0 ? 0 : 1;
}
