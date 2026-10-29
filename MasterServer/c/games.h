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
//   0   join   uuid\0 pos:u8 pl_p\0
//   1   leave  uuid\0 pos:u8
//   2   save   uuid\0 xp:f64 checkpoint:u8 petals..0 failed_crafts..0
//              [mob_kills..0 loadout..0 nickname\0]
//   3   build log (accepted and ignored)
//   101 server id (rivet lobby id, accepted and ignored)
// master -> game server (first byte MS_API_SUCCESS=45, then opcode):
//   0 alias\0
//   1 pos username\0 xp checkpoint petals..0 failed_crafts..0
//     mob_kills..0 loadout..0 nickname\0 is_dev:u8
//   2 pos uuid\0         (kick that slot: denied join or session taken over)
// counts are (id:u8, rarity:u8, count:varuint) terminated by id 0; loadout is
// (slot:u8 1-based, id:u8, rarity:u8) terminated by slot 0.

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
