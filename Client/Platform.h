#pragma once

#include <stdint.h>

struct rr_game;

// Where the page runs, decided by Client/public/platform.js before the game
// starts: 0 on a plain web page, 1 when embedded on CrazyGames or running with
// ?useLocalSdk=true (the SDK's test mode, which behaves like CrazyGames).
uint8_t rr_platform_on_crazygames(void);

// The CrazyGames "disable chat" setting. Always 0 off CrazyGames.
uint8_t rr_platform_chat_disabled(void);

// Runs once per frame. On every page it joins the squad an invite link points
// to. On CrazyGames it also reports what the game is doing to the SDK
// (loading, gameplay, the squad as a room that friends can join), and acts on
// what the SDK reports back (the logged in user, instant multiplayer).
void rr_platform_tick(struct rr_game *game, float delta);

// Copies a link that takes whoever opens it into the squad with this code.
void rr_platform_copy_invite_link(char const *squad_code);

// The "Login with CrazyGames" button. Only logs the player in.
void rr_crazygames_login(void);

// Starts the CrazyGames login (if the player is not logged in already), then
// reconnects with the player's user token so the master server can link the
// account. Does nothing off CrazyGames or if the player cancels.
void rr_crazygames_link(void *game);

// Called from JS with malloc'ed strings, which it frees.
void rr_crazygames_on_link(char *uuid, char *token, char *cg_token,
                           void *game);

// Requests to the master server's public /auth endpoints, see Oauth2.c.
void rr_auth_post(void *game, char const *path, char const *field,
                  char const *value, uint8_t connect_anyway);
void rr_auth_on_result(char *uuid, char *token, char *code, char *message,
                       int flags, void *game);
// Makes a code (shown in game->transfer_code) that moves this account to
// another site, and moves the account behind game->enter_code to this one.
void rr_auth_get_transfer_code(void *game);
void rr_auth_use_transfer_code(void *game);
