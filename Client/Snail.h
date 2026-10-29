// Copyright (C) 2024 Paul Johnson
// Copyright (C) 2024-2025 Maxim Nesterov

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

// The snail run on the client: what the Garden server says about it
// (Server/Snail.c), the snail hole drawn in the world and on the minimap, and
// the lines of the feed. The banner is Ui/InGame/SnailHud.c.

struct rr_game;
struct rr_game_snail_run;
struct proto_bug;

// the snail run is going, and the snail hole is on the map
uint8_t rr_snail_client_running(struct rr_game *);
void rr_snail_client_reset(struct rr_game *);
// the snail closest to the player, or NULL if none is going
struct rr_game_snail_run *rr_snail_client_focus(struct rr_game *);
void rr_snail_client_read_state(struct rr_game *, struct proto_bug *);
// once a frame, delta is in seconds
void rr_snail_client_tick(struct rr_game *, float delta);
// in the camera's world space, after the floor
void rr_snail_client_render_world(struct rr_game *);
// on the minimap, see rr_ctf_minimap_overlay for what the numbers mean
void rr_snail_minimap_overlay(struct rr_game *, float size, float view_x,
                              float view_y, float span);

// the banner, in Ui/InGame/SnailHud.c
struct rr_ui_element;
extern struct rr_ui_element *rr_ui_snail_hud_init(struct rr_game *);
