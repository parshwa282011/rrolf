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

#pragma once

#include <stddef.h>

// Discord OAuth2 (identify + email) and bot direct messages. All calls block
// (libcurl) and must run on a worker thread.

struct ms_discord_user
{
    char id[64];
    char username[128]; // global display name when set, else username
    char email[256];    // may be empty
};

int ms_discord_oauth_enabled();
int ms_discord_dm_enabled();

int ms_discord_authorize_url(char const *state, char *out, size_t size);
// 0 on success
int ms_discord_exchange_code(char const *code, struct ms_discord_user *out);
// 0 if Discord accepted the message, -1 otherwise (e.g. the user blocks DMs)
int ms_discord_send_dm(char const *user_id, char const *content);

// shared with other outgoing HTTPS users; call once at startup / shutdown
void ms_http_global_init();
void ms_http_global_cleanup();

// percent-encodes into out (always NUL terminated); returns length or -1
int ms_url_encode(char *out, size_t size, char const *in);
