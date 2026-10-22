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

#include <Shared/Ctf.h>
#include <Shared/Entity.h>

// Capture the flag, server side. There are two layers.
//
// CtfRules.c is the rules and knows nothing about the server: teams, flag
// points, the flags, taking, banking, setting up, elimination and winning. It
// is given where the players are each tick and can be tested on its own.
//
// Ctf.c connects it to the server: linking squads, spawning, respawning, the
// phases, the packets.
//
// The flags are not entities. They are state in rr_ctf_match that is sent to
// every client each tick, so they need no collision, wall or visibility rules.
//
// Rules:
// - Every team has RR_CTF_FLAGS_PER_TEAM flags, standing on its flag points.
// - A player takes a flag that another team holds by touching it and carries it
//   to any of their own team's flag points to bank it.
// - A team holds the flags standing on its points and the flags it banked. A
//   flag that is carried or on the ground still counts for the team it was
//   taken from.
// - A banked flag can be set up on one of the team's empty flag points, by
//   standing on it for a few seconds. It still counts, but can be taken again.
// - A team that holds fewer than RR_CTF_MIN_FLAGS flags is out at once. What it
//   had banked goes to the team that took its last flag.
// - A team wins when it holds every flag of every other team.

#define RR_CTF_FLAG_COUNT (RR_CTF_MAX_TEAMS * RR_CTF_FLAGS_PER_TEAM)
#define RR_CTF_NONE 255

struct rr_ctf_flag
{
    uint8_t owner;  // the team it was made for
    uint8_t index;  // which of the owner's three
    uint8_t state;  // rr_ctf_flag_state
    uint8_t holder; // the team that holds it
    uint8_t point;  // standing: which flag point of the holder
    uint8_t home_point; // dropped: the point of the holder it goes back to
    int8_t carrier; // carried: the player slot, or -1
    uint16_t return_ticks;
    float x;
    float y;
};

struct rr_ctf_team
{
    uint8_t active;
    uint8_t eliminated;
    uint8_t squad;
    uint8_t quadrant;
    uint8_t last_taker; // the team that last took a flag from this one
    uint8_t point_set[RR_CTF_FLAGS_PER_TEAM];
    float point_x[RR_CTF_FLAGS_PER_TEAM];
    float point_y[RR_CTF_FLAGS_PER_TEAM];
    // the flag standing on each point, or -1 when it is empty
    int8_t point_flag[RR_CTF_FLAGS_PER_TEAM];
    uint8_t setup_ticks[RR_CTF_FLAGS_PER_TEAM];
};

struct rr_ctf_event
{
    uint8_t type;
    uint8_t a;
    uint8_t b;
    uint8_t flag;
};

struct rr_ctf_match
{
    uint8_t phase;
    uint8_t team_count;
    uint8_t winner;
    uint8_t flags_ready; // the flags exist, from the end of the placement
    uint32_t phase_ticks; // counts down, except in the war where it counts up
    struct rr_ctf_team teams[RR_CTF_MAX_TEAMS];
    struct rr_ctf_flag flags[RR_CTF_FLAG_COUNT];
    // the team of each squad, RR_CTF_NONE when it is not in the match
    uint8_t squad_team[RR_SQUAD_COUNT];
    // the host's squad, only meaningful while team_count > 0
    uint8_t host_squad;
    // the last few events, newest last, and a counter clients use to see new
    // ones
    struct rr_ctf_event events[RR_CTF_EVENT_HISTORY];
    uint16_t event_seq;
};

// where a player is, given to the rules each tick. The index is the player's
// slot on the server.
struct rr_ctf_player
{
    uint8_t present; // connected and in the match
    uint8_t alive;
    uint8_t team;
    float x;
    float y;
};

// ---- CtfRules.c
void rr_ctf_match_reset(struct rr_ctf_match *);
// the point is not checked here, the caller checks it against the map
void rr_ctf_set_point(struct rr_ctf_match *, uint8_t team, uint8_t index,
                      float x, float y);
uint8_t rr_ctf_all_points_set(struct rr_ctf_match *);
// creates the flags on the points, every point has to be set
void rr_ctf_create_flags(struct rr_ctf_match *);
// one tick of the rules, from the first tick the flags exist
void rr_ctf_step(struct rr_ctf_match *, struct rr_ctf_player *players);
// how many flags the team holds, and how many of other teams' it holds
uint8_t rr_ctf_held(struct rr_ctf_match *, uint8_t team);
uint8_t rr_ctf_held_foreign(struct rr_ctf_match *, uint8_t team);
// how many foreign flags the team has banked, and the points not yet set up
uint8_t rr_ctf_banked(struct rr_ctf_match *, uint8_t team);
// the player's flag, or -1
int8_t rr_ctf_carried_by(struct rr_ctf_match *, uint8_t player);
void rr_ctf_push_event(struct rr_ctf_match *, uint8_t type, uint8_t a,
                       uint8_t b, uint8_t flag);
// the team ended the match by giving up or by every player leaving
void rr_ctf_eliminate(struct rr_ctf_match *, uint8_t team, uint8_t by);

struct rr_server;
struct rr_server_client;
struct proto_bug;

// ---- Ctf.c
// after the simulation tick
void rr_ctf_tick(struct rr_server *);
// to one client, every tick
void rr_ctf_send_state(struct rr_server *, struct rr_server_client *);
// the team a client plays for, RR_CTF_NONE if they are not in the match
uint8_t rr_ctf_client_team(struct rr_server *, struct rr_server_client *);
// the match is running or about to, so the squad can't change
uint8_t rr_ctf_squad_locked(struct rr_server *, uint8_t squad);
void rr_ctf_spawn_position(struct rr_server *, struct rr_server_client *,
                           float *x, float *y);
// the packets from the host and players. The reader is past the header
void rr_ctf_on_create(struct rr_server *, struct rr_server_client *,
                      struct proto_bug *);
void rr_ctf_on_start(struct rr_server *, struct rr_server_client *);
void rr_ctf_on_cancel(struct rr_server *, struct rr_server_client *);
void rr_ctf_on_place_flag(struct rr_server *, struct rr_server_client *,
                          struct proto_bug *);
void rr_ctf_on_respawn(struct rr_server *, struct rr_server_client *);
// the Enter Game / Leave Game button. Always handled in this mode
void rr_ctf_on_ready(struct rr_server *, struct rr_server_client *);
