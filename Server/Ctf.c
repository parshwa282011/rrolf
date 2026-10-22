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

#include <Server/Ctf.h>

#ifdef CTF

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Server/Client.h>
#include <Server/Server.h>
#include <Server/Simulation.h>
#include <Server/Squad.h>

#include <Shared/Component/Flower.h>
#include <Shared/Component/Physical.h>
#include <Shared/Component/PlayerInfo.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>
#include <Shared/pb.h>

// Everything that ties the rules in CtfRules.c to the server, see Server/Ctf.h
// for the overview. One match at a time per server process.

// how long a message to the host or a player is sent for, in ticks
#define MESSAGE_TICKS (3 * RR_CTF_TPS)

// phase lengths can be shortened with environment variables, so that a test
// does not take ten minutes
static uint32_t seconds_from_env(char const *name, uint32_t fallback)
{
    char const *value = getenv(name);
    if (value == NULL || value[0] == 0)
        return fallback;
    long seconds = strtol(value, NULL, 10);
    return seconds > 0 ? (uint32_t)seconds : fallback;
}

static uint32_t placement_ticks()
{
    return RR_CTF_TPS * seconds_from_env("RR_CTF_PLACEMENT_SECONDS",
                                         RR_CTF_PLACEMENT_SECONDS);
}

static uint32_t farm_ticks()
{
    return RR_CTF_TPS *
           seconds_from_env("RR_CTF_FARM_SECONDS", RR_CTF_FARM_SECONDS);
}

static uint32_t ended_ticks()
{
    return RR_CTF_TPS *
           seconds_from_env("RR_CTF_ENDED_SECONDS", RR_CTF_ENDED_SECONDS);
}

static void say(struct rr_server_client *client, uint8_t message, uint8_t arg)
{
    client->ctf_message = message;
    client->ctf_message_arg = arg;
    client->ctf_message_ticks = MESSAGE_TICKS;
}

uint8_t rr_ctf_client_team(struct rr_server *this,
                           struct rr_server_client *client)
{
    if (!client->in_squad)
        return RR_CTF_NONE;
    return this->ctf.squad_team[client->squad];
}

uint8_t rr_ctf_squad_locked(struct rr_server *this, uint8_t squad)
{
    return this->ctf.squad_team[squad] != RR_CTF_NONE &&
           this->ctf.phase != rr_ctf_phase_lobby;
}

static uint8_t running(struct rr_ctf_match *m)
{
    return m->phase == rr_ctf_phase_placement ||
           m->phase == rr_ctf_phase_farm || m->phase == rr_ctf_phase_war;
}

static uint8_t flower_alive(struct rr_server *this,
                            struct rr_server_client *client)
{
    return client->player_info != NULL &&
           rr_simulation_entity_alive(&this->simulation,
                                      client->player_info->flower_id) &&
           !is_dead_flower(&this->simulation, client->player_info->flower_id);
}

// is the client the leader of their squad, which has to be a private one. The
// leader of the first squad is the host, every leader places their team's flags
static uint8_t is_leader(struct rr_server *this, struct rr_server_client *client)
{
    return client->in_squad && this->squads[client->squad].private &&
           this->squads[client->squad].owner == client->squad_pos;
}

// ---- spawning

void rr_ctf_spawn_position(struct rr_server *this,
                           struct rr_server_client *client, float *x, float *y)
{
    uint8_t team = rr_ctf_client_team(this, client);
    uint8_t quadrant =
        team == RR_CTF_NONE ? 0 : this->ctf.teams[team].quadrant;
    struct rr_ctf_rect *spawn = &RR_CTF_SPAWNS[quadrant];
    float cell = rr_ctf_cell_size();
    *x = (spawn->x + rr_frand() * spawn->w) * cell;
    *y = (spawn->y + rr_frand() * spawn->h) * cell;
}

// the loadout can name petals that were crafted away since, only what the
// player still has stays
static void validate_loadout(struct rr_server_client *client,
                             struct rr_squad_member *member)
{
    uint64_t left[rr_petal_id_max][rr_rarity_id_max];
    memcpy(left, client->inventory, sizeof left);
    for (uint32_t i = 0; i < RR_MAX_SLOT_COUNT * 2; ++i)
    {
        uint8_t id = member->loadout[i].id;
        uint8_t rarity = member->loadout[i].rarity;
        if (id == 0)
            continue;
        if (id >= rr_petal_id_max || rarity >= rr_rarity_id_max ||
            left[id][rarity] == 0)
            member->loadout[i].id = member->loadout[i].rarity = 0;
        else
            --left[id][rarity];
    }
}

// a player who never picked petals gets the starting ones in their slots
static void fill_empty_loadout(struct rr_squad_member *member)
{
    for (uint32_t i = 0; i < RR_MAX_SLOT_COUNT; ++i)
        if (member->loadout[i].id != 0)
            return;
    for (uint32_t i = 0; i < RR_CTF_START_PETALS && i < RR_MAX_SLOT_COUNT; ++i)
    {
        member->loadout[i].id = rr_petal_id_basic;
        member->loadout[i].rarity = rr_rarity_id_epic;
    }
}

// puts the player into the game at their base, as a new flower. Also how a
// player respawns, which picks up any change to their loadout
static void spawn_member(struct rr_server *this,
                         struct rr_server_client *client)
{
    struct rr_squad_member *member = rr_squad_get_client_slot(this, client);
    if (member == NULL || client->disconnected)
        return;
    if (client->player_info != NULL)
    {
        rr_simulation_request_entity_deletion(&this->simulation,
                                              client->player_info->parent_id);
        client->player_info = NULL;
    }
    member->playing = 1;
    validate_loadout(client, member);
    fill_empty_loadout(member);
    rr_server_client_create_player_info(this, client);
    rr_server_client_create_flower(client);
    client->ctf_respawn_ticks = 0;
}

static void remove_from_game(struct rr_server *this,
                             struct rr_server_client *client)
{
    if (client->player_info != NULL)
    {
        rr_simulation_request_entity_deletion(&this->simulation,
                                              client->player_info->parent_id);
        client->player_info = NULL;
    }
    struct rr_squad_member *member = rr_squad_get_client_slot(this, client);
    if (member != NULL)
        member->playing = 0;
}

// ---- the lobby

static void unlink_all(struct rr_server *this)
{
    rr_ctf_match_reset(&this->ctf);
}

// the player in a slot of the team's squad, NULL if the slot is empty
static struct rr_server_client *member_client(struct rr_server *this,
                                              uint8_t team, uint8_t slot)
{
    struct rr_squad_member *member =
        &this->squads[this->ctf.teams[team].squad].members[slot];
    return member->in_use ? member->client : NULL;
}

// runs the statement that follows once for every player of a team
#define FOR_EACH_MEMBER(THIS, TEAM, CLIENT)                                    \
    for (uint8_t _slot = 0; _slot < RR_SQUAD_MEMBER_COUNT; ++_slot)            \
        for (struct rr_server_client *CLIENT =                                 \
                 member_client(THIS, TEAM, _slot);                             \
             CLIENT != NULL; CLIENT = NULL)

static void end_match(struct rr_server *this)
{
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        if (!this->ctf.teams[t].active)
            continue;
        FOR_EACH_MEMBER(this, t, client) remove_from_game(this, client);
    }
    rr_ctf_reset_walls();
    unlink_all(this);
}

void rr_ctf_on_create(struct rr_server *this, struct rr_server_client *client,
                      struct proto_bug *encoder)
{
    struct rr_ctf_match *m = &this->ctf;
    uint8_t count = proto_bug_read_uint8(encoder, "ctf count");
    char codes[RR_CTF_MAX_TEAMS - 1][16] = {{0}};
    for (uint8_t i = 0; i < count && i < RR_CTF_MAX_TEAMS - 1; ++i)
        proto_bug_read_string(encoder, codes[i], 15, "ctf code");
    if (count > RR_CTF_MAX_TEAMS - 1)
        count = RR_CTF_MAX_TEAMS - 1;
    if (!is_leader(this, client))
    {
        say(client, rr_ctf_message_not_host, 0);
        return;
    }
    if (m->phase != rr_ctf_phase_lobby ||
        (m->team_count > 0 && m->host_squad != client->squad))
    {
        say(client, rr_ctf_message_busy, 0);
        return;
    }
    uint8_t squads[RR_CTF_MAX_TEAMS];
    uint8_t n = 0;
    squads[n++] = client->squad;
    for (uint8_t i = 0; i < count; ++i)
    {
        // the code can be typed with its prefix
        char *code = codes[i];
        char *dash = strchr(code, '-');
        if (dash != NULL)
            code = dash + 1;
        if (code[0] == 0)
            continue; // an empty field is not a team
        uint8_t found = RR_CTF_NONE;
        if (strlen(code) == 6)
            for (uint32_t s = 0; s < RR_SQUAD_COUNT; ++s)
                if (this->squads[s].member_count > 0 &&
                    memcmp(this->squads[s].squad_code, code, 6) == 0)
                    found = s;
        if (found == RR_CTF_NONE)
        {
            say(client, rr_ctf_message_bad_code, i + 1);
            return;
        }
        if (!this->squads[found].private)
        {
            say(client, rr_ctf_message_not_private, i + 1);
            return;
        }
        for (uint8_t k = 0; k < n; ++k)
            if (squads[k] == found)
            {
                say(client, rr_ctf_message_same_squad, i + 1);
                return;
            }
        squads[n++] = found;
    }
    unlink_all(this);
    m->host_squad = client->squad;
    m->team_count = n;
    for (uint8_t t = 0; t < n; ++t)
    {
        m->teams[t].active = 1;
        m->teams[t].squad = squads[t];
        m->teams[t].quadrant = rr_ctf_team_quadrant(t);
        m->squad_team[squads[t]] = t;
    }
    printf("[ctf] %u squads linked by %s\n", n, client->rivet_account.uuid);
    say(client, rr_ctf_message_linked, n);
}

void rr_ctf_on_cancel(struct rr_server *this, struct rr_server_client *client)
{
    struct rr_ctf_match *m = &this->ctf;
    if (!is_leader(this, client) || m->team_count == 0 ||
        m->host_squad != client->squad)
    {
        say(client, rr_ctf_message_not_host, 0);
        return;
    }
    // the host ends a running match too, everyone goes back to the menu
    printf("[ctf] cancelled by the host\n");
    end_match(this);
    say(client, rr_ctf_message_cancelled, 0);
}

void rr_ctf_on_start(struct rr_server *this, struct rr_server_client *client)
{
    struct rr_ctf_match *m = &this->ctf;
    if (!is_leader(this, client) || m->team_count == 0 ||
        m->host_squad != client->squad)
    {
        say(client, rr_ctf_message_not_host, 0);
        return;
    }
    if (m->phase != rr_ctf_phase_lobby)
    {
        say(client, rr_ctf_message_busy, 0);
        return;
    }
    if (m->team_count < 2)
    {
        say(client, rr_ctf_message_need_teams, 0);
        return;
    }
    for (uint8_t t = 0; t < m->team_count; ++t)
        if (this->squads[m->teams[t].squad].member_count == 0)
        {
            say(client, rr_ctf_message_empty_team, t + 1);
            return;
        }
    printf("[ctf] match starts with %u teams\n", m->team_count);
    rr_ctf_reset_walls();
    memset(m->flags, 0, sizeof m->flags);
    m->flags_ready = 0;
    m->winner = RR_CTF_NONE;
    for (uint8_t t = 0; t < m->team_count; ++t)
    {
        struct rr_ctf_team *team = &m->teams[t];
        team->eliminated = 0;
        team->last_taker = RR_CTF_NONE;
        memset(team->point_set, 0, sizeof team->point_set);
        memset(team->setup_ticks, 0, sizeof team->setup_ticks);
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            team->point_flag[i] = -1;
    }
    m->phase = rr_ctf_phase_placement;
    m->phase_ticks = placement_ticks();
    for (uint8_t t = 0; t < m->team_count; ++t)
        FOR_EACH_MEMBER(this, t, member) spawn_member(this, member);
}

// ---- flag points

// a position on open ground. The inverted corner tiles (value & 8) are walls
static uint8_t open_at(float x, float y)
{
    struct rr_maze_declaration *decl = &RR_MAZES[rr_biome_id_ctf];
    if (x < 0 || y < 0)
        return 0;
    uint32_t gx = x / decl->grid_size, gy = y / decl->grid_size;
    if (gx >= decl->maze_dim || gy >= decl->maze_dim)
        return 0;
    struct rr_maze_grid *grid = &decl->maze[gy * decl->maze_dim + gx];
    return grid->value != 0 && (grid->value & 8) == 0;
}

void rr_ctf_on_place_flag(struct rr_server *this,
                          struct rr_server_client *client,
                          struct proto_bug *encoder)
{
    struct rr_ctf_match *m = &this->ctf;
    uint8_t index = proto_bug_read_uint8(encoder, "flag index");
    float x = proto_bug_read_float32(encoder, "flag x");
    float y = proto_bug_read_float32(encoder, "flag y");
    uint8_t team = rr_ctf_client_team(this, client);
    if (team == RR_CTF_NONE || m->phase != rr_ctf_phase_placement ||
        m->teams[team].eliminated || index >= RR_CTF_FLAGS_PER_TEAM ||
        !isfinite(x) || !isfinite(y))
        return;
    if (!is_leader(this, client))
    {
        say(client, rr_ctf_message_not_leader, 0);
        return;
    }
    // inside the team's area, on open ground with some room around it
    struct rr_ctf_rect *area = &RR_CTF_QUADRANTS[m->teams[team].quadrant];
    float cell = rr_ctf_cell_size();
    if (x < area->x * cell || x >= (area->x + area->w) * cell ||
        y < area->y * cell || y >= (area->y + area->h) * cell ||
        !open_at(x, y) || !open_at(x + 60, y) || !open_at(x - 60, y) ||
        !open_at(x, y + 60) || !open_at(x, y - 60))
    {
        say(client, rr_ctf_message_bad_spot, 0);
        return;
    }
    struct rr_ctf_team *t = &m->teams[team];
    for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        if (i != index && t->point_set[i] &&
            hypotf(t->point_x[i] - x, t->point_y[i] - y) <
                RR_CTF_POINT_SPACING)
        {
            say(client, rr_ctf_message_too_close, 0);
            return;
        }
    rr_ctf_set_point(m, team, index, x, y);
    // everyone is done, no need to wait for the rest of the time
    if (rr_ctf_all_points_set(m) &&
        m->phase_ticks > 5 * RR_CTF_TPS)
        m->phase_ticks = 5 * RR_CTF_TPS;
}

// the flags a leader did not place go to the default spots of the area
static void place_missing_points(struct rr_ctf_match *m)
{
    float cell = rr_ctf_cell_size();
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        struct rr_ctf_team *team = &m->teams[t];
        if (!team->active)
            continue;
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            if (!team->point_set[i])
                rr_ctf_set_point(
                    m, t, i,
                    (RR_CTF_DEFAULT_FLAGS[team->quadrant][i][0] + 0.5f) * cell,
                    (RR_CTF_DEFAULT_FLAGS[team->quadrant][i][1] + 0.5f) *
                        cell);
    }
}

// ---- the other buttons

void rr_ctf_on_respawn(struct rr_server *this, struct rr_server_client *client)
{
    // dying on purpose, to come back with a changed loadout right away
    if (!running(&this->ctf) || rr_ctf_client_team(this, client) == RR_CTF_NONE ||
        !flower_alive(this, client))
        return;
    rr_component_flower_set_dead(
        rr_simulation_get_flower(&this->simulation,
                                 client->player_info->flower_id),
        &this->simulation, 1);
}

void rr_ctf_on_ready(struct rr_server *this, struct rr_server_client *client)
{
    struct rr_ctf_match *m = &this->ctf;
    uint8_t team = rr_ctf_client_team(this, client);
    if (team == RR_CTF_NONE)
    {
        say(client, rr_ctf_message_not_member, 0);
        return;
    }
    struct rr_squad_member *member = rr_squad_get_client_slot(this, client);
    if (!running(m))
        return;
    if (member->playing)
    {
        // leaving. A flag the player carries drops where they stand
        remove_from_game(this, client);
        return;
    }
    if (!m->teams[team].eliminated)
        spawn_member(this, client);
}

// ---- the tick

static void write_players(struct rr_server *this,
                          struct rr_ctf_player *players)
{
    memset(players, 0, sizeof(struct rr_ctf_player) * RR_MAX_CLIENT_COUNT);
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        if (!rr_bitset_get(this->clients_in_use, i))
            continue;
        struct rr_server_client *client = &this->clients[i];
        uint8_t team = rr_ctf_client_team(this, client);
        if (team == RR_CTF_NONE || client->disconnected)
            continue;
        players[i].team = team;
        players[i].present = client->player_info != NULL;
        players[i].alive = flower_alive(this, client);
        if (players[i].alive)
        {
            struct rr_component_physical *physical = rr_simulation_get_physical(
                &this->simulation, client->player_info->flower_id);
            players[i].x = physical->x;
            players[i].y = physical->y;
        }
    }
}

// the lobby only holds squads that still exist. A squad that emptied might
// have been made again by someone else, so it must not stay linked
static void drop_vanished_squads(struct rr_server *this)
{
    struct rr_ctf_match *m = &this->ctf;
    if (m->team_count == 0 || m->phase != rr_ctf_phase_lobby)
        return;
    if (this->squads[m->host_squad].member_count == 0)
    {
        unlink_all(this);
        return;
    }
    for (uint8_t t = 1; t < m->team_count; ++t)
    {
        if (this->squads[m->teams[t].squad].member_count > 0)
            continue;
        m->squad_team[m->teams[t].squad] = RR_CTF_NONE;
        for (uint8_t k = t; k + 1 < m->team_count; ++k)
        {
            m->teams[k] = m->teams[k + 1];
            m->teams[k].quadrant = rr_ctf_team_quadrant(k);
            m->squad_team[m->teams[k].squad] = k;
        }
        memset(&m->teams[m->team_count - 1], 0, sizeof m->teams[0]);
        m->teams[m->team_count - 1].squad = RR_CTF_NONE;
        --m->team_count;
        --t;
    }
}

void rr_ctf_tick(struct rr_server *this)
{
    struct rr_ctf_match *m = &this->ctf;
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        if (!rr_bitset_get(this->clients_in_use, i))
            continue;
        struct rr_server_client *client = &this->clients[i];
        if (client->ctf_message_ticks > 0 && --client->ctf_message_ticks == 0)
            client->ctf_message = rr_ctf_message_none;
        // a player in the game who left their squad is no longer anybody's
        if (!client->in_squad && client->player_info != NULL)
            remove_from_game(this, client);
    }
    drop_vanished_squads(this);
    switch (m->phase)
    {
    case rr_ctf_phase_lobby:
        return;
    case rr_ctf_phase_placement:
        if (m->phase_ticks > 0)
            --m->phase_ticks;
        if (m->phase_ticks == 0)
        {
            place_missing_points(m);
            rr_ctf_create_flags(m);
            m->phase = rr_ctf_phase_farm;
            m->phase_ticks = farm_ticks();
            printf("[ctf] flags placed, the walls stay up for %u s\n",
                   m->phase_ticks / RR_CTF_TPS);
        }
        break;
    case rr_ctf_phase_farm:
        if (m->phase_ticks > 0)
            --m->phase_ticks;
        if (m->phase_ticks == 0)
        {
            rr_ctf_drop_walls();
            rr_ctf_push_event(m, rr_ctf_event_walls, 0, 0, 0);
            m->phase = rr_ctf_phase_war;
            m->phase_ticks = 0;
            printf("[ctf] the walls dropped\n");
        }
        break;
    case rr_ctf_phase_war:
        ++m->phase_ticks;
        break;
    case rr_ctf_phase_ended:
        if (m->phase_ticks > 0)
            --m->phase_ticks;
        if (m->phase_ticks == 0)
        {
            printf("[ctf] match over, back to the lobby\n");
            end_match(this);
        }
        return;
    }
    // a team whose players all left is out. When nobody is left the match is
    // over, or the server would stay in it for ever
    uint8_t anyone = 0;
    for (uint8_t t = 0; t < m->team_count; ++t)
    {
        if (this->squads[m->teams[t].squad].member_count > 0)
            anyone = 1;
        else if (!m->teams[t].eliminated && m->flags_ready)
            rr_ctf_eliminate(m, t, RR_CTF_NONE);
    }
    if (!anyone)
    {
        printf("[ctf] nobody is left, the match is over\n");
        end_match(this);
        return;
    }
    // the rules
    uint8_t was_eliminated[RR_CTF_MAX_TEAMS];
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
        was_eliminated[t] = m->teams[t].eliminated;
    struct rr_ctf_player players[RR_MAX_CLIENT_COUNT];
    write_players(this, players);
    rr_ctf_step(m, players);
    // a team that is out has its players killed, and they stay dead
    for (uint8_t t = 0; t < m->team_count; ++t)
    {
        if (!m->teams[t].eliminated)
            continue;
        if (!was_eliminated[t])
            printf("[ctf] team %s is out\n", rr_ctf_team_name(t));
        FOR_EACH_MEMBER(this, t, client)
        {
            if (flower_alive(this, client))
                rr_component_flower_set_dead(
                    rr_simulation_get_flower(&this->simulation,
                                             client->player_info->flower_id),
                    &this->simulation, 1);
        }
    }
    if (m->winner != RR_CTF_NONE)
    {
        printf("[ctf] team %s won\n", rr_ctf_team_name(m->winner));
        m->phase = rr_ctf_phase_ended;
        m->phase_ticks = ended_ticks();
        return;
    }
    // players who died are back at their base after a moment
    for (uint8_t t = 0; t < m->team_count; ++t)
    {
        if (m->teams[t].eliminated)
            continue;
        FOR_EACH_MEMBER(this, t, client)
        {
            if (client->disconnected || client->player_info == NULL)
                continue;
            if (flower_alive(this, client))
            {
                client->ctf_respawn_ticks = 0;
                continue;
            }
            if (++client->ctf_respawn_ticks >= RR_CTF_RESPAWN_TICKS)
                spawn_member(this, client);
        }
    }
}

// ---- the state sent to the clients

void rr_ctf_send_state(struct rr_server *this, struct rr_server_client *client)
{
    struct rr_ctf_match *m = &this->ctf;
    uint8_t team = rr_ctf_client_team(this, client);
    uint8_t host = is_leader(this, client) && team != RR_CTF_NONE &&
                   m->host_squad == client->squad;
    // players outside the match only need to know that one is going
    uint8_t body = team != RR_CTF_NONE;
    struct proto_bug encoder;
    proto_bug_init(&encoder, outgoing_message);
    proto_bug_write_uint8(&encoder, rr_clientbound_ctf_state, "header");
    proto_bug_write_uint8(&encoder, m->phase, "ctf phase");
    proto_bug_write_uint8(&encoder, m->team_count, "ctf teams");
    proto_bug_write_uint8(&encoder, team, "ctf my team");
    // bit 0 walls are down, 1 the client is the host, 2 the host can start, 3 the
    // rest of the packet follows, 4 the client leads their squad, 5 the flags
    // exist
    uint8_t can_start =
        host && m->phase == rr_ctf_phase_lobby && m->team_count >= 2;
    proto_bug_write_uint8(
        &encoder,
        rr_ctf_walls_dropped() | host << 1 | can_start << 2 | body << 3 |
            is_leader(this, client) << 4 | m->flags_ready << 5,
        "ctf flags");
    proto_bug_write_uint8(&encoder, m->winner, "ctf winner");
    proto_bug_write_uint8(&encoder, client->ctf_message, "ctf message");
    proto_bug_write_uint8(&encoder, client->ctf_message_arg, "ctf arg");
    proto_bug_write_varuint(&encoder, m->phase_ticks, "ctf ticks");
    proto_bug_write_varuint(&encoder, m->event_seq, "ctf event seq");
    for (uint8_t e = 0; e < RR_CTF_EVENT_HISTORY; ++e)
    {
        proto_bug_write_uint8(&encoder, m->events[e].type, "ctf event");
        proto_bug_write_uint8(&encoder, m->events[e].a, "ctf event a");
        proto_bug_write_uint8(&encoder, m->events[e].b, "ctf event b");
        proto_bug_write_uint8(&encoder, m->events[e].flag, "ctf event flag");
    }
    if (body)
    {
        for (uint8_t t = 0; t < m->team_count; ++t)
        {
            struct rr_ctf_team *team_state = &m->teams[t];
            proto_bug_write_uint8(&encoder, team_state->eliminated,
                                  "ctf team out");
            proto_bug_write_uint8(&encoder, team_state->squad,
                                  "ctf team squad");
            proto_bug_write_uint8(&encoder,
                                  this->squads[team_state->squad].member_count,
                                  "ctf team members");
            proto_bug_write_uint8(&encoder, rr_ctf_held(m, t), "ctf held");
            proto_bug_write_uint8(&encoder, rr_ctf_banked(m, t), "ctf banked");
            for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            {
                proto_bug_write_uint8(&encoder, team_state->point_set[i],
                                      "ctf point set");
                proto_bug_write_float32(&encoder, team_state->point_x[i],
                                        "ctf point x");
                proto_bug_write_float32(&encoder, team_state->point_y[i],
                                        "ctf point y");
                proto_bug_write_uint8(
                    &encoder,
                    team_state->setup_ticks[i] * 100 / RR_CTF_SETUP_TICKS,
                    "ctf setup");
            }
        }
        if (m->flags_ready)
            for (uint8_t t = 0; t < m->team_count; ++t)
                for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
                {
                    struct rr_ctf_flag *flag =
                        &m->flags[t * RR_CTF_FLAGS_PER_TEAM + i];
                    proto_bug_write_uint8(&encoder, flag->state, "ctf flag state");
                    proto_bug_write_uint8(&encoder, flag->holder, "ctf flag holder");
                    proto_bug_write_float32(&encoder, flag->x, "ctf flag x");
                    proto_bug_write_float32(&encoder, flag->y, "ctf flag y");
                }
    }
    rr_server_client_write_message(client, encoder.start,
                                   encoder.current - encoder.start);
}

#endif
