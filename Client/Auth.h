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

// Account login against the master server (see MasterServer/c). Logging in,
// signing up, confirming the email and resetting a password are NOT done here:
// they are the static page Client/public/login.html. The game only checks that
// the browser holds a working login and sends it to /login when it does not.
//
// localStorage keys (shared with the gardn client on the same origin):
//   uuid  PL-T  PL-P
// PL-T never expires and is only ever sent to the master. PL-P lasts a week:
// it is what the game server receives together with the uuid. When PL-P has
// expired the player is sent to the log in page again.

struct rr_game;

enum rr_auth_screen
{
    rr_auth_screen_none,    // logged in (or game not asking yet)
    rr_auth_screen_checking // validating the stored session / on the way to /login
};

struct rr_auth_state
{
    uint8_t screen;
    char message[256];
};

// call once at startup: validates the stored session, or sends the browser to
// /login (also when a social sign in returned with #oauth_code=...)
void rr_auth_start(struct rr_game *);

void rr_auth_link_discord(struct rr_game *);
// forgets the login and goes to /login
void rr_auth_logout(struct rr_game *);

// called from JavaScript when the session check finished (exported)
void rr_auth_on_response(int status, char *uuid, char *pl_p, char *username);
