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

#include "api.h"

// The leaderboard page (rrolf/Client/public/leaderboard.html) and the numbers
// behind it.
//
// Looking through every save to rank players would be slow, so the numbers are
// kept next to the saves, in the leaderboards section: one row per player and
// board (board, uuid, score, {"name"}). A row is written when the player's own
// data is: when they join, and when a game server or gardn saves them (a player
// who is offline changes nothing). Counters that no save holds (how often the
// player tried to craft an Ancient, kills in gardn) are added to from the
// changes the servers send. When the master starts it walks through every
// account once and puts right whatever the saves can prove.
//
// Rysteria boards:  rr_level  rr_ancient_attempts  rr_pvp_best  rr_ancients
//                   rr_ancient_kills  rr_mythics  rr_kills
// Gardn boards:     gardn_kills  gardn_best_score  gardn_mythics

#define MS_LB_RYSTERIA_BOARDS 7

// The values of the Rysteria boards that follow from a save (see
// ms_lb_rysteria_values): [0] level with the way to the next one as a fraction,
// [1] unused (attempts are counted, not derived), [2] pvp points now, [3] Ancient
// petals owned, [4] Ancients killed, [5] Mythic petals owned, [6] kills.
void ms_lb_rysteria_values(struct ms_db_rysteria const *rys,
                           double out[MS_LB_RYSTERIA_BOARDS]);

// Writes a player's Rysteria numbers. `prev` holds what was written last (-1 =
// not known) and only what changed is written; NULL writes everything. `now`
// receives what is on the boards afterwards. `pvp_best` is the highest score the
// game server saw, `attempts` the Ancient craft attempts made since the last
// save. Returns 0 when everything went through. Any thread.
int ms_lb_push_rysteria(char const *uuid, char const *username,
                        struct ms_db_rysteria const *rys, int64_t pvp_best,
                        uint64_t attempts, double const *prev,
                        double now[MS_LB_RYSTERIA_BOARDS]);

// gardn's numbers: kills since its last report, and (when `have_stats`) the
// Mythic petals in the player's saves and their best save. Any thread.
int ms_lb_push_gardn(char const *uuid, uint64_t kills, int have_stats,
                     uint64_t mythics, uint64_t best_score);

// walks through every account on a worker thread and corrects the boards
void ms_lb_verify_start();

void ms_h_leaderboard(struct ms_req *, struct ms_res *);
