# rysteria master server (C)

Accounts, login tokens and persistent game data for rrolf and gardn. It replaces
the Node master in `../main.js` (still there until you say it can go).

One process, one port (`PORT`, default 55554): libwebsockets serves the HTTPS/JSON
API and the game-server WebSocket. Slow work (database, argon2, Discord) runs on
worker threads, never on the event loop.

## Build and run

```sh
cmake -S . -B build && cmake --build build -j
build/rysteria-master --env /path/to/master.env
```
Needs libwebsockets, libsodium, libcurl and at least one of libpq / mongo-c-driver
(`scripts/<os>/setup.sh` installs them). `.env.example` lists every setting.
`DB_TYPE=postgres|mongo` picks the backend at start-up.

## Storage

Four sections, same logical fields in both databases (PostgreSQL: one schema each,
MongoDB: one collection each): **accounts** (accounts + links), **rysteria**
(xp, checkpoint, petals, failed_crafts, mob_kills, loadout, nickname, keyed by
uuid), **gardn** (progress saves keyed by uuid + save id), **leaderboards**
(generic placeholder). A new engine = one `db_<name>.c` implementing `struct ms_db`
plus a `case` in `db.c`.

## Tokens

* **PL-T** – never expires; HMAC over (uuid, hash of the argon2id string, per
  account `token_key`). Only ever sent to this server.
* **PL-P** – valid one week; fixed 80 byte layout (version, username padded to 64,
  issue time) XORed with a keystream and sealed with an HMAC tag, so the server can
  read the issue time back but nobody can forge one. Sent to game servers.
* Changing the password or "log out everywhere" rotates `token_key`: both die.

## HTTP API

Public (browser, CORS allow-list, rate limited): `POST /auth/signup`, `/auth/login`,
`/auth/session`, `/auth/logout-all`, `/auth/account`, `/auth/complete-profile`,
`/auth/reset/{start,send,confirm}`, `/auth/unlink/<provider>`,
`GET /oauth/discord/{start,callback}`, `POST /oauth/discord/link-start`,
`/oauth/exchange`, `GET /health`. Google, Apple and email answer
`503 provider_unavailable` (the email service is not working yet).

Server only (`X-Server-Secret`): `POST /srv/verify`, `/srv/gardn/{load,flush}`,
`/srv/leaderboard/submit`, `GET /srv/leaderboard/top`.

## Game server WebSocket

Same binary protocol as the old master, opcodes documented at the top of
`games.h`. The secret goes in the `X-Server-Secret` upgrade header (the old
`/api/<secret>` path still works).

## Tests

```sh
build/tests/test_core                         # encoding, wire format, tokens
DB_TYPE=postgres POSTGRES_URI=... build/tests/test_db   # backend conformance
DB_TYPE=mongo MONGO_URI=... build/tests/test_db
SERVER_SECRET=... node tests/e2e.js           # against a running master
```
`test_db` and `e2e.js` create accounts: use a throwaway database.
