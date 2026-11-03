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

`GET /health` answers `{"ok","db","node","backup"}` (503 when the database is down).
Backup servers only (`X-Clone-Secret` = `CLONE_SECRET`): `GET /backup?since=&source=&node=`,
the change feed described in `db_postgres.c` (PostgreSQL only). Used by
`dual-server-syncing/`.

Server only (`X-Server-Secret`): `POST /srv/verify`, `/srv/gardn/{load,flush}`,
`/srv/leaderboard/submit`, `GET /srv/leaderboard/top`.

## Game server WebSocket

Same binary protocol as the old master, opcodes documented at the top of
`games.h`. The secret goes in the `X-Server-Secret` upgrade header (the old
`/api/<secret>` path still works).

Besides players, game servers send the master their **chat** (op 6) and their **squads** (op 7, every two
seconds) and it hands both to every other game server, so chat is global and a squad list covers all servers.
A game server names itself in op 101 (`RR_SERVER_NAME`, the name in `servers.conf`).

**Restarting a dropped game server.** When a game server drops without sending op 8 ("I am stopping", sent
on SIGTERM/SIGINT) the master runs `RESTART_COMMAND` once, with `{name}` replaced by the server's name
(`scripts/common/restart-one.sh`, written into the master's `.env` by `build.sh`). If the command fails, or
the server is not connected again after `RESTART_WAIT_SECONDS` (default 90), it gives up; the next drop tries
again. Empty `RESTART_COMMAND` = never. The log lines start with `[restart]`.

## Leaderboards

`GET /leaderboard[?game=rysteria|gardn][&me=<uuid>]` (public, CORS allow-list) answers what the pages
`Client/public/leaderboard.html` show: the boards of one game (both without `game`), the best ten of each (name and
score, no uuids) and, with `me`, where that player stands on each board. The top lists are cached for 8 seconds for
everybody. The pages are `/leaderboard` (Rysteria) and `/gardn/leaderboard` (gardn), see `scripts/common/edge.js`.

**Rate limit, on the master, not in the pages:** every route a browser can reach shares one bucket of
`MS_PUBLIC_PER_MINUTE` (60) requests a minute per address (`CF-Connecting-IP`), and each route has its own, tighter,
bucket as well (`api.c`): the leaderboards, profiles and stats 60 a minute, the search 30. The answer is 429.

`GET /profile?uuid=<uuid>` or `?name=<username>` is the public page of an account (`/stats?uuid=...`, `/status`):
username, level, the Rysteria boards with the place on each, the highest PvP score, the loadout, and the minigames
record (`ctf`, `tdm`, `has`: wins, played, lost, win percent). No email, links or tokens, and no dev accounts.
`GET /profile/search?q=<start of a name>` lists up to ten usernames (case-insensitive prefix).

**Minigames** (`rrolf-minigames`) never save anything but this: when a match ends the game server sends op 9 (mode,
PIN, and per player uuid and won). It counts only if the PIN equals `MINIGAMES_PIN` of the master (empty = nothing
counts). Played and won are the boards `mg_<mode>_played` (hidden) and `mg_<mode>_wins` (on the Rysteria page).

The numbers are kept in the leaderboards section (`board, uuid, score, {"name"}`) so that ranking never reads the
saves. **Rysteria:** `rr_level` (level and how far to the next, as one number), `rr_pvp_kills`, `rr_pvp_best`,
`rr_ancient_attempts`, `rr_ancients` (owned now), `rr_ancient_kills`, `rr_kills` (mobs). **Gardn:** `gardn_kills` (players killed),
`gardn_best_score`, `gardn_mythics` (in all saves). They are written

* when a player **joins** a Rysteria server, and when a game server **saves** them (every few seconds while they play):
  everything a save can prove is set exactly (ancients owned can go down), the PvP best only goes up, and the Ancient
  craft attempts and the players killed in PvP that the game server counted (the end of the save message) are added;
* when gardn reports a player (with their saves, once a minute): kills are added, Mythic petals and best score set;
* a player who is offline changes nothing;
* **on start-up** a worker walks through every account once and corrects the boards from the saves (levels, owned and
  killed Ancients, kills, best gardn score). Counters no save holds (craft attempts, PvP kills, gardn kills, gardn Mythics, the
  PvP peak) are never lowered. `LEADERBOARD_VERIFY=0` turns it off.

## Tests

```sh
build/tests/test_core                         # encoding, wire format, tokens
DB_TYPE=postgres POSTGRES_URI=... build/tests/test_db   # backend conformance
DB_TYPE=mongo MONGO_URI=... build/tests/test_db
SERVER_SECRET=... node tests/e2e.js           # against a running master
node tests/leaderboard.js                     # saves -> boards -> /leaderboard (a throwaway master, see the file)
```
`test_db` and `e2e.js` create accounts: use a throwaway database.
