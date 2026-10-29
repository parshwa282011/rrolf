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

// Account login against the master server (see MasterServer/c).
//
// localStorage keys (shared with the gardn client on the same origin):
//   uuid  PL-T  PL-P
// PL-T never expires and is only ever sent to the master. PL-P lasts a week:
// it is what the game server receives together with the uuid. When PL-P has
// expired the player must log in again (password or Discord).

struct rr_game;

enum rr_auth_screen
{
    rr_auth_screen_none,           // logged in (or game not asking yet)
    rr_auth_screen_checking,       // validating the stored session
    rr_auth_screen_login,
    rr_auth_screen_signup,
    rr_auth_screen_reset_identify, // "which account?"
    rr_auth_screen_reset_code,     // pick a method, enter the OTP, new password
    rr_auth_screen_profile         // after Discord: choose username + password
};

struct rr_auth_state
{
    uint8_t screen;
    uint8_t busy;
    uint8_t reset_discord_available;
    uint8_t reset_code_sent;
    // text fields (kept in sync with the DOM inputs by Ui/TextInput.c)
    char identifier[255];
    char email[255];
    char username[65];
    char password[129];
    char otp[16];
    char new_password[129];
    // shown under the form
    char message[256];
    char notice[256];
    char reset_id[64];
    char pending_id[64];
};

// call once at startup: handles a Discord return (#oauth_code=...), else
// validates the stored session, else shows the login screen
void rr_auth_start(struct rr_game *);

void rr_auth_login(struct rr_game *);
void rr_auth_signup(struct rr_game *);
void rr_auth_discord(struct rr_game *);
void rr_auth_link_discord(struct rr_game *);
void rr_auth_reset_start(struct rr_game *);
void rr_auth_reset_send(struct rr_game *);
void rr_auth_reset_confirm(struct rr_game *);
void rr_auth_complete_profile(struct rr_game *);
void rr_auth_logout(struct rr_game *);
void rr_auth_show(struct rr_game *, enum rr_auth_screen);

// called from JavaScript when a request to the master finished (exported)
void rr_auth_on_response(int kind, int status, char *error, char *message,
                         char *uuid, char *pl_t, char *pl_p, char *username,
                         char *extra1, char *extra2, char *extra3);
