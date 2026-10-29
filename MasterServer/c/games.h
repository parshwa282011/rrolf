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

#include <libwebsockets.h>

// rrolf game server link (binary websocket protocol "g"). Everything here
// runs on the event loop thread except ms_games_kick_uuid().
//
// Opcodes, game server -> master (first byte):
//   0   join   uuid\0 token\0 (the player's PL-P) code\0 (unused) nonce:varuint
//              pos:u8
//   1   leave  uuid\0
//   2   save   uuid\0 xp:f64 pvp_points:varuint checkpoint:u8 x3 (hell creek,
//              pvp, garden) petals..0 failed_crafts..0 mob_gallery..0
//              [loadout..0 nickname\0]
//   3   build log (accepted and ignored)
//   4   CrazyGames score (ignored)
//   5   account request pos:u8 nonce:varuint kind:u8 crazygames:u8 payload\0
//       (answered with "not available", accounts live on the login screen)
//   101 server id (rivet lobby id, accepted and ignored)
// master -> game server (first byte MS_API_SUCCESS=45, then opcode):
//   0 alias\0
//   1 pos nonce:varuint uuid\0 token\0 username\0 id\0 cg_id\0 is_dev:u8 xp:f64
//     pvp_points:varuint checkpoint:u8 x3 petals..0 failed_crafts..0
//     mob_gallery..0 loadout..0 nickname\0
//   2 pos nonce:varuint  (kick that slot: denied join or session taken over)
//   3 pos nonce ok kind message\0 uuid\0 token\0 code\0  (answer to op 5)
// counts are (id:u8, rarity:u8, count:varuint) terminated by id 0; mob gallery
// ids are the mob id + 1; loadout is (slot:u8 1-based, id:u8, rarity:u8)
// terminated by slot 0.

void ms_games_set_context(struct lws_context *);
void ms_games_init();
// protocol callback for protocol name "g"
int ms_games_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);
// event loop: flush timers, join timeouts, queued kicks
void ms_games_tick();
size_t ms_games_session_size();
// writes every unsaved player to the database; call with no jobs running
void ms_games_shutdown();
int ms_games_online_count();
