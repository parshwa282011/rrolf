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

#include <stdint.h>

enum ms_db_type
{
    ms_db_type_postgres,
    ms_db_type_mongo,
    ms_db_type_max
};

struct ms_config
{
    enum ms_db_type db_type;
    char postgres_uri[512];
    char mongo_uri[512];
    char mongo_db_name[64];
    uint16_t port;
    char bind_address[64]; // "" = all interfaces
    char allowed_origins[512]; // comma separated, exact match
    char server_secret[128];
    char clone_secret[128]; // X-Clone-Secret for /backup (backup servers)
    char node_name[48];     // this machine's name, shown by /health
    char token_secret_1[128];
    char token_secret_2[128];
    char otp_pepper[128];
    char minigames_pin[64]; // "" = no match of the minigames server counts
    char client_url[256]; // where OAuth redirects send the browser
    char discord_client_id[64];
    char discord_client_secret[128];
    char discord_bot_token[128];
    char discord_redirect_uri[256];
    // Google and Apple sign in; both off while their ids are empty
    char google_client_id[128];
    char google_client_secret[128];
    char google_redirect_uri[256];
    char apple_client_id[128]; // the Services ID
    char apple_team_id[32];
    char apple_key_id[32];
    char apple_private_key_file[256]; // the .p8 file Apple gave
    char apple_domain_file[256];      // the domain verification file Apple gave, served at /.well-known/
    char apple_redirect_uri[256];
    // outgoing email (codes for password and username changes): an HTTPS API
    // that takes a JSON body, see email.c
    char email_api_url[256];
    char email_api_key[256];
    char email_api_auth[128]; // header line, {key} is replaced
    char email_api_body[1024]; // JSON body, {from} {to} {subject} {text}
    char email_from[128];
    char email_from_name[64];
    // EMAIL_PROVIDER=ses sends through Amazon SES instead (signed requests)
    char email_provider[16];
    char aws_access_key_id[64];
    char aws_secret_access_key[128];
    char aws_region[32];
    uint32_t require_email_verified; // 1: an address has to be proven before use
    uint32_t email_daily_limit; // emails sent per UTC day, 0 = no limit
    uint32_t flush_interval_ms;
    uint32_t db_pool_size;
    char trust_proxy_header[64]; // e.g. CF-Connecting-IP, "" = use peer ip
    uint32_t pl_p_lifetime_seconds;
    // run (once) when a game server drops its connection without saying it is
    // shutting down; {name} is replaced by the server's name. "" = never
    char restart_command[512];
    // how long to wait for it to come back before giving up
    uint32_t restart_wait_seconds;
    // 1 = on start-up every account is checked against its saves and the
    // leaderboards are corrected (leaderboard.c)
    uint32_t leaderboard_verify;
};

extern struct ms_config MS_CONFIG;

// Reads KEY=VALUE lines from `path` into the process environment (without
// overriding variables that are already set), then fills MS_CONFIG from the
// environment. Returns 0 on success, -1 (after logging why) on failure.
// A missing file is not an error: the environment may be fully populated.
int ms_config_load(char const *path);

// Fills MS_CONFIG from the environment only.
int ms_config_from_env();
