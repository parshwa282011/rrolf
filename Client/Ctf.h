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

// Capture the flag on the client: what the server says about the match, the
// flags drawn in the world, the keys a leader uses, the packets to the server.
// The server side is Server/Ctf.c.

struct rr_game;
struct proto_bug;
struct rr_renderer;

// the game is connected to the capture the flag server
uint8_t rr_ctf_client_selected(struct rr_game *);
// in the game itself, not on the menu, of a capture the flag match
uint8_t rr_ctf_client_in_game(struct rr_game *);
void rr_ctf_client_reset(struct rr_game *);
void rr_ctf_client_read_state(struct rr_game *, struct proto_bug *);
// once a frame, delta is in seconds
void rr_ctf_client_tick(struct rr_game *, float delta);
// in the camera's world space, after the floor
void rr_ctf_client_render_world(struct rr_game *);
// a color to draw over the floor at a world position, 0 for none. It shows
// which team's area it is
uint32_t rr_ctf_client_floor_tint(struct rr_game *, float x, float y);
// on the minimap: the team areas, the players in their team's color, the flag
// points and the flags. size is the minimap box in pixels. It shows span world
// units across, centered on view_x and view_y
void rr_ctf_minimap_overlay(struct rr_game *, float size, float view_x,
                            float view_y, float span);
// the team that plays from a squad, 255 if none
uint8_t rr_ctf_client_team_of_squad(struct rr_game *, uint8_t squad);
// the host's feedback, and the line of text for the top of the screen
char const *rr_ctf_client_message_text(struct rr_game *, char *out, uint32_t size);
// 0:00 style time
void rr_ctf_client_format_time(char *out, uint32_t size, uint32_t seconds);

// the packets to the server
void rr_ctf_client_send_create(struct rr_game *);
void rr_ctf_client_send_start(struct rr_game *);
void rr_ctf_client_send_cancel(struct rr_game *);
void rr_ctf_client_send_respawn(struct rr_game *);

// the host panel and the HUD, in Client/Ui
struct rr_ui_element;
extern struct rr_ui_element *rr_ui_ctf_toggle_button_init();
extern struct rr_ui_element *rr_ui_ctf_host_container_init(struct rr_game *);
extern struct rr_ui_element *rr_ui_ctf_hud_init(struct rr_game *);
extern struct rr_ui_element *rr_ui_ctf_loadout_init(struct rr_game *);
