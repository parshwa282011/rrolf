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

#include <Client/Ctf.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Client/Filter.h>
#include <Client/Game.h>
#include <Client/InputData.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Simulation.h>
#include <Client/Socket.h>
#include <Shared/Bitset.h>
#include <Shared/Ctf.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>
#include <Shared/pb.h>

uint8_t rr_ctf_client_selected(struct rr_game *this)
{
    return this->selected_biome == rr_biome_id_ctf;
}

uint8_t rr_ctf_client_in_game(struct rr_game *this)
{
    return rr_ctf_client_selected(this) && this->simulation_ready;
}

void rr_ctf_client_reset(struct rr_game *this)
{
    // the codes the host typed stay, everything the server told us goes
    char codes[RR_CTF_MAX_TEAMS - 1][16];
    memcpy(codes, this->ctf.codes, sizeof codes);
    memset(&this->ctf, 0, sizeof this->ctf);
    memcpy(this->ctf.codes, codes, sizeof codes);
    this->ctf.my_team = RR_CTF_NONE;
    this->ctf.winner = RR_CTF_NONE;
    // a new connection has its own maze
    if (rr_ctf_walls_dropped())
    {
        rr_ctf_reset_walls();
        rr_minimap_invalidate();
    }
}

uint8_t rr_ctf_client_team_of_squad(struct rr_game *this, uint8_t squad)
{
    for (uint8_t t = 0; t < this->ctf.team_count; ++t)
        if (this->ctf.has_body && this->ctf.teams[t].squad == squad)
            return t;
    return RR_CTF_NONE;
}

void rr_ctf_client_format_time(char *out, uint32_t size, uint32_t seconds)
{
    snprintf(out, size, "%u:%02u", seconds / 60, seconds % 60);
}

char const *rr_ctf_client_message_text(struct rr_game *this, char *out,
                                       uint32_t size)
{
    uint8_t arg = this->ctf.message_arg;
    switch (this->ctf.message)
    {
    case rr_ctf_message_linked:
        snprintf(out, size, "%u squads linked. Press Start when everyone is ready.", arg);
        break;
    case rr_ctf_message_bad_code:
        snprintf(out, size, "Squad %u: no squad has that code.", arg + 1);
        break;
    case rr_ctf_message_not_private:
        snprintf(out, size, "Squad %u is not private, only private squads can play.", arg + 1);
        break;
    case rr_ctf_message_same_squad:
        snprintf(out, size, "Squad %u is already in the match.", arg + 1);
        break;
    case rr_ctf_message_not_host:
        snprintf(out, size, "Only the leader of a private squad can do that.");
        break;
    case rr_ctf_message_need_teams:
        snprintf(out, size, "Link at least one other squad first.");
        break;
    case rr_ctf_message_empty_team:
        snprintf(out, size, "Team %u has no players left.", arg);
        break;
    case rr_ctf_message_busy:
        snprintf(out, size, "Another match is already set up on this server.");
        break;
    case rr_ctf_message_not_leader:
        snprintf(out, size, "Only your squad's leader places the flags.");
        break;
    case rr_ctf_message_bad_spot:
        snprintf(out, size, "A flag can't go there. Stand on open ground in your area.");
        break;
    case rr_ctf_message_too_close:
        snprintf(out, size, "Too close to another of your flags.");
        break;
    case rr_ctf_message_not_member:
        snprintf(out, size, "Join a match first: create a private squad and have the host link it.");
        break;
    case rr_ctf_message_cancelled:
        snprintf(out, size, "Match cancelled.");
        break;
    default:
        out[0] = 0;
        return NULL;
    }
    return out;
}

// ---- the state packet

static void add_feed_line(struct rr_game *this, char const *text,
                          uint32_t color)
{
    struct rr_game_ctf *c = &this->ctf;
    memmove(&c->feed[1], &c->feed[0], sizeof c->feed[0] * 4);
    snprintf(c->feed[0].text, sizeof c->feed[0].text, "%s", text);
    c->feed[0].color = color;
    c->feed[0].age = 0;
}

static void show_event(struct rr_game *this, struct rr_game_ctf_event *e)
{
    char text[96];
    char const *a = rr_ctf_team_name(e->a);
    char const *b = rr_ctf_team_name(e->b);
    uint32_t color = rr_ctf_team_color(e->a);
    switch (e->type)
    {
    case rr_ctf_event_taken:
        snprintf(text, sizeof text, "%s took a flag from %s", a, b);
        break;
    case rr_ctf_event_banked:
        snprintf(text, sizeof text, "%s banked a flag of %s", a, b);
        break;
    case rr_ctf_event_dropped:
        snprintf(text, sizeof text, "%s dropped a flag of %s", a, b);
        color = 0xffdddddd;
        break;
    case rr_ctf_event_returned:
        snprintf(text, sizeof text, "A flag of %s went back", b);
        color = rr_ctf_team_color(e->b);
        break;
    case rr_ctf_event_setup:
        snprintf(text, sizeof text, "%s set up a flag", a);
        break;
    case rr_ctf_event_eliminated:
        snprintf(text, sizeof text, "%s is eliminated!", a);
        color = 0xffff6060;
        break;
    case rr_ctf_event_won:
        snprintf(text, sizeof text, "%s wins!", a);
        break;
    case rr_ctf_event_walls:
        snprintf(text, sizeof text, "The walls dropped, take the flags!");
        color = 0xffffffff;
        break;
    default:
        return;
    }
    add_feed_line(this, text, color);
}

void rr_ctf_client_read_state(struct rr_game *this, struct proto_bug *d)
{
    struct rr_game_ctf *c = &this->ctf;
    c->active = 1;
    c->phase = proto_bug_read_uint8(d, "ctf phase");
    c->team_count = proto_bug_read_uint8(d, "ctf teams");
    c->my_team = proto_bug_read_uint8(d, "ctf my team");
    uint8_t flags = proto_bug_read_uint8(d, "ctf flags");
    c->walls_dropped = flags & 1;
    c->is_host = flags >> 1 & 1;
    c->can_start = flags >> 2 & 1;
    c->has_body = flags >> 3 & 1;
    c->is_leader = flags >> 4 & 1;
    c->flags_ready = flags >> 5 & 1;
    c->winner = proto_bug_read_uint8(d, "ctf winner");
    c->message = proto_bug_read_uint8(d, "ctf message");
    c->message_arg = proto_bug_read_uint8(d, "ctf arg");
    c->phase_ticks = proto_bug_read_varuint(d, "ctf ticks");
    uint16_t seq = proto_bug_read_varuint(d, "ctf event seq");
    for (uint8_t e = 0; e < RR_CTF_EVENT_HISTORY; ++e)
    {
        c->events[e].type = proto_bug_read_uint8(d, "ctf event");
        c->events[e].a = proto_bug_read_uint8(d, "ctf event a");
        c->events[e].b = proto_bug_read_uint8(d, "ctf event b");
        c->events[e].flag = proto_bug_read_uint8(d, "ctf event flag");
    }
    // events newer than the last packet become lines in the feed
    if (!c->event_seq_known)
    {
        c->event_seq_known = 1;
        c->event_seq = seq;
    }
    else if (seq != c->event_seq)
    {
        uint16_t fresh = (uint16_t)(seq - c->event_seq);
        if (fresh > RR_CTF_EVENT_HISTORY)
            fresh = RR_CTF_EVENT_HISTORY;
        for (uint16_t k = RR_CTF_EVENT_HISTORY - fresh; k < RR_CTF_EVENT_HISTORY;
             ++k)
            show_event(this, &c->events[k]);
        c->event_seq = seq;
    }
    // the walls are in the maze the renderer and the minimap read
    if (c->walls_dropped && !rr_ctf_walls_dropped())
    {
        rr_ctf_drop_walls();
        rr_minimap_invalidate();
    }
    else if (!c->walls_dropped && rr_ctf_walls_dropped())
    {
        rr_ctf_reset_walls();
        rr_minimap_invalidate();
    }
    if (!c->has_body)
        return;
    for (uint8_t t = 0; t < c->team_count; ++t)
    {
        struct rr_game_ctf_team *team = &c->teams[t];
        team->eliminated = proto_bug_read_uint8(d, "ctf team out");
        team->squad = proto_bug_read_uint8(d, "ctf team squad");
        team->members = proto_bug_read_uint8(d, "ctf team members");
        team->held = proto_bug_read_uint8(d, "ctf held");
        team->banked = proto_bug_read_uint8(d, "ctf banked");
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        {
            team->point_set[i] = proto_bug_read_uint8(d, "ctf point set");
            team->point_x[i] = proto_bug_read_float32(d, "ctf point x");
            team->point_y[i] = proto_bug_read_float32(d, "ctf point y");
            team->setup[i] = proto_bug_read_uint8(d, "ctf setup");
        }
    }
    if (c->flags_ready)
        for (uint8_t t = 0; t < c->team_count; ++t)
            for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            {
                struct rr_game_ctf_flag *flag =
                    &c->flags[t * RR_CTF_FLAGS_PER_TEAM + i];
                flag->state = proto_bug_read_uint8(d, "ctf flag state");
                flag->holder = proto_bug_read_uint8(d, "ctf flag holder");
                flag->x = proto_bug_read_float32(d, "ctf flag x");
                flag->y = proto_bug_read_float32(d, "ctf flag y");
                if (!c->flags_drawn)
                {
                    flag->lerp_x = flag->x;
                    flag->lerp_y = flag->y;
                }
            }
    if (c->flags_ready)
        c->flags_drawn = 1;
}

// ---- the packets to the server

static struct proto_bug begin_packet(struct rr_game *this, uint8_t header)
{
    struct proto_bug encoder;
    proto_bug_init(&encoder, RR_OUTGOING_PACKET);
    proto_bug_write_uint8(&encoder, this->socket.quick_verification, "qv");
    proto_bug_write_uint8(&encoder, header, "header");
    return encoder;
}

void rr_ctf_client_send_create(struct rr_game *this)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_ctf_create);
    uint8_t count = 0;
    for (uint8_t i = 0; i < RR_CTF_MAX_TEAMS - 1; ++i)
        if (this->ctf.codes[i][0] != 0)
            ++count;
    proto_bug_write_uint8(&encoder, count, "ctf count");
    for (uint8_t i = 0; i < RR_CTF_MAX_TEAMS - 1; ++i)
        if (this->ctf.codes[i][0] != 0)
            proto_bug_write_string(&encoder, this->ctf.codes[i], 16,
                                   "ctf code");
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

void rr_ctf_client_send_start(struct rr_game *this)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_ctf_start);
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

void rr_ctf_client_send_cancel(struct rr_game *this)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_ctf_cancel);
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

void rr_ctf_client_send_respawn(struct rr_game *this)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_ctf_respawn);
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

static void send_place_flag(struct rr_game *this, uint8_t index, float x,
                            float y)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_ctf_place_flag);
    proto_bug_write_uint8(&encoder, index, "flag index");
    proto_bug_write_float32(&encoder, x, "flag x");
    proto_bug_write_float32(&encoder, y, "flag y");
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

// the loadout is only sent from the menu, in a match it changes while playing
static void send_squad_update(struct rr_game *this)
{
    struct proto_bug encoder = begin_packet(this, rr_serverbound_squad_update);
    char nickname[17];
    snprintf(nickname, sizeof nickname, "%s", this->cache.nickname);
    rr_filter_censor(nickname);
    proto_bug_write_string(&encoder, nickname, 16, "nickname");
    proto_bug_write_uint8(&encoder, this->slots_unlocked, "loadout count");
    for (uint32_t i = 0; i < this->slots_unlocked; ++i)
    {
        proto_bug_write_uint8(&encoder, this->cache.loadout[i].id, "id");
        proto_bug_write_uint8(&encoder, this->cache.loadout[i].rarity, "rarity");
        proto_bug_write_uint8(
            &encoder, this->cache.loadout[i + RR_MAX_SLOT_COUNT].id, "id");
        proto_bug_write_uint8(
            &encoder, this->cache.loadout[i + RR_MAX_SLOT_COUNT].rarity,
            "rarity");
    }
    rr_websocket_send(&this->socket, encoder.current - encoder.start);
}

static uint32_t loadout_hash(struct rr_game *this)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < RR_MAX_SLOT_COUNT * 2; ++i)
    {
        hash = (hash ^ this->cache.loadout[i].id) * 16777619u;
        hash = (hash ^ this->cache.loadout[i].rarity) * 16777619u;
    }
    return hash;
}

// ---- once a frame

static uint8_t flower_position(struct rr_game *this, float *x, float *y)
{
    if (this->player_info == NULL ||
        this->player_info->flower_id == RR_NULL_ENTITY ||
        !rr_simulation_has_flower(this->simulation,
                                  this->player_info->flower_id) ||
        rr_simulation_get_flower(this->simulation,
                                 this->player_info->flower_id)->dead)
        return 0;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(this->simulation,
                                   this->player_info->flower_id);
    *x = physical->lerp_x;
    *y = physical->lerp_y;
    return 1;
}

void rr_ctf_client_tick(struct rr_game *this, float delta)
{
    struct rr_game_ctf *c = &this->ctf;
    if (!rr_ctf_client_selected(this) || !c->active)
        return;
    c->anim += delta;
    for (uint8_t i = 0; i < 5; ++i)
        c->feed[i].age += delta;
    if (c->start_confirm > 0)
        c->start_confirm -= delta;
    // flags glide to where the server says they are
    float follow = 1 - powf(0.0005f, delta);
    for (uint8_t f = 0; f < RR_CTF_MAX_TEAMS * RR_CTF_FLAGS_PER_TEAM; ++f)
    {
        struct rr_game_ctf_flag *flag = &c->flags[f];
        flag->lerp_x += (flag->x - flag->lerp_x) * follow;
        flag->lerp_y += (flag->y - flag->lerp_y) * follow;
    }
    if (!rr_ctf_client_in_game(this) || this->text_input_focused ||
        this->chat.chat_active)
        return;
    // the loadout can be changed during a match, tell the server a moment after
    // the last change. It takes effect when the player respawns
    uint32_t hash = loadout_hash(this);
    if (hash != c->loadout_sent)
    {
        c->loadout_wait += delta;
        if (c->loadout_wait > 0.3f && this->socket_ready)
        {
            c->loadout_sent = hash;
            c->loadout_wait = 0;
            send_squad_update(this);
        }
    }
    else
        c->loadout_wait = 0;
    // a leader places the team's flag points with F, where they stand
    if (c->phase == rr_ctf_phase_placement && c->is_leader &&
        c->my_team < RR_CTF_MAX_TEAMS &&
        rr_bitset_get_bit(this->input_data->keys_pressed_this_tick, 'F'))
    {
        float x, y;
        if (!flower_position(this, &x, &y))
            return;
        struct rr_game_ctf_team *team = &c->teams[c->my_team];
        uint8_t index = RR_CTF_FLAGS_PER_TEAM;
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            if (!team->point_set[i])
            {
                index = i;
                break;
            }
        if (index == RR_CTF_FLAGS_PER_TEAM)
        {
            // all are placed, F moves them in turn
            index = c->place_cycle % RR_CTF_FLAGS_PER_TEAM;
            c->place_cycle = (index + 1) % RR_CTF_FLAGS_PER_TEAM;
        }
        send_place_flag(this, index, x, y);
    }
}

// ---- drawing

uint32_t rr_ctf_client_floor_tint(struct rr_game *this, float x, float y)
{
    struct rr_game_ctf *c = &this->ctf;
    if (!rr_ctf_client_selected(this) || !c->active || !c->has_body)
        return 0;
    float cell = rr_ctf_cell_size();
    int32_t tx = floorf(x / cell), ty = floorf(y / cell);
    for (uint8_t t = 0; t < c->team_count; ++t)
    {
        struct rr_ctf_rect *area = &RR_CTF_QUADRANTS[rr_ctf_team_quadrant(t)];
        if (tx >= area->x && tx < area->x + area->w && ty >= area->y &&
            ty < area->y + area->h)
            return (rr_ctf_team_color(t) & 0x00ffffff) | 0x26000000;
    }
    return 0;
}

static void draw_flag(struct rr_renderer *renderer, uint32_t owner_color,
                      uint32_t holder_color, float alpha)
{
    rr_renderer_set_global_alpha(renderer, alpha);
    rr_renderer_set_line_join(renderer, 1);
    // the base, in the color of whoever holds the flag
    rr_renderer_set_fill(renderer, holder_color);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_line_width(renderer, 5);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 0, 22);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
    // the pole and the cloth, in the color of the team it belongs to
    rr_renderer_set_stroke(renderer, 0xff3a2a18);
    rr_renderer_set_line_width(renderer, 9);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 0, 0);
    rr_renderer_line_to(renderer, 0, -120);
    rr_renderer_stroke(renderer);
    rr_renderer_set_fill(renderer, owner_color);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_line_width(renderer, 5);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, 4, -118);
    rr_renderer_line_to(renderer, 84, -92);
    rr_renderer_line_to(renderer, 4, -62);
    rr_renderer_line_to(renderer, 4, -118);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
    rr_renderer_set_global_alpha(renderer, 1);
}

void rr_ctf_client_render_world(struct rr_game *this)
{
    struct rr_game_ctf *c = &this->ctf;
    if (!rr_ctf_client_selected(this) || !c->active || !c->has_body ||
        c->team_count == 0)
        return;
    struct rr_renderer *renderer = this->renderer;
    struct rr_renderer_context_state state;
    float pulse = 0.5f + 0.5f * sinf(c->anim * 6);
    // the flag points: a ring, filled when something stands on it. An empty one
    // of your own team that you can set a banked flag on shows its progress
    for (uint8_t t = 0; t < c->team_count; ++t)
    {
        struct rr_game_ctf_team *team = &c->teams[t];
        uint32_t color = rr_ctf_team_color(t);
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        {
            if (!team->point_set[i])
                continue;
            rr_renderer_context_state_init(renderer, &state);
            rr_renderer_translate(renderer, team->point_x[i], team->point_y[i]);
            rr_renderer_set_fill(renderer, (color & 0x00ffffff) | 0x30000000);
            rr_renderer_set_stroke(renderer, color);
            rr_renderer_set_line_width(renderer, 8);
            rr_renderer_begin_path(renderer);
            rr_renderer_arc(renderer, 0, 0, RR_CTF_BANK_RADIUS);
            rr_renderer_fill(renderer);
            rr_renderer_stroke(renderer);
            if (team->setup[i] > 0)
            {
                rr_renderer_set_stroke(renderer, 0xffffffff);
                rr_renderer_set_line_width(renderer, 16);
                rr_renderer_begin_path(renderer);
                rr_renderer_partial_arc(
                    renderer, 0, 0, RR_CTF_BANK_RADIUS + 18, -M_PI / 2,
                    -M_PI / 2 + 2 * M_PI * team->setup[i] / 100.0f, 0);
                rr_renderer_stroke(renderer);
            }
            rr_renderer_context_state_free(renderer, &state);
        }
    }
    if (!c->flags_ready)
        return;
    for (uint8_t f = 0; f < c->team_count * RR_CTF_FLAGS_PER_TEAM; ++f)
    {
        struct rr_game_ctf_flag *flag = &c->flags[f];
        if (flag->state == rr_ctf_flag_banked)
            continue;
        uint8_t owner = f / RR_CTF_FLAGS_PER_TEAM;
        rr_renderer_context_state_init(renderer, &state);
        rr_renderer_translate(renderer, flag->lerp_x, flag->lerp_y);
        float alpha = 1;
        if (flag->state == rr_ctf_flag_carried)
        {
            // above whoever carries it, so that they can be seen
            rr_renderer_translate(renderer, 0, -50);
            rr_renderer_scale(renderer, 0.8f);
        }
        else if (flag->state == rr_ctf_flag_dropped)
            alpha = 0.55f + 0.45f * pulse;
        draw_flag(renderer, rr_ctf_team_color(owner),
                  rr_ctf_team_color(flag->holder), alpha);
        rr_renderer_context_state_free(renderer, &state);
    }
}

// ---- the minimap

void rr_ctf_minimap_overlay(struct rr_game *this, float size, float world_size)
{
    struct rr_game_ctf *c = &this->ctf;
    if (!c->active || !c->has_body || c->team_count == 0)
        return;
    struct rr_renderer *renderer = this->renderer;
    float cell = rr_ctf_cell_size();
    float to_px = size / world_size;
    for (uint8_t t = 0; t < c->team_count; ++t)
    {
        struct rr_ctf_rect *area = &RR_CTF_QUADRANTS[rr_ctf_team_quadrant(t)];
        rr_renderer_set_fill(renderer,
                             (rr_ctf_team_color(t) & 0x00ffffff) | 0x38000000);
        rr_renderer_begin_path(renderer);
        rr_renderer_fill_rect(renderer, size * -0.5f + area->x * cell * to_px,
                              size * -0.5f + area->y * cell * to_px,
                              area->w * cell * to_px, area->h * cell * to_px);
    }
    // everyone in the arena, in the color of their team
    rr_renderer_set_line_width(renderer, 1.5f);
    rr_renderer_set_stroke(renderer, 0xff222222);
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        if (this->arena_player_infos[i] == RR_NULL_ENTITY)
            continue;
        struct rr_component_player_info *player_info =
            rr_simulation_get_player_info(this->simulation,
                                          this->arena_player_infos[i]);
        if (player_info->arena != this->player_info->arena ||
            player_info->flower_id == RR_NULL_ENTITY)
            continue;
        uint8_t team = rr_ctf_client_team_of_squad(this, player_info->squad);
        if (team == RR_CTF_NONE)
            continue;
        struct rr_component_physical *physical = rr_simulation_get_physical(
            this->simulation, player_info->flower_id);
        rr_renderer_set_fill(renderer, rr_ctf_team_color(team));
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, size * (physical->lerp_x / world_size - 0.5f),
                        size * (physical->lerp_y / world_size - 0.5f), 3);
        rr_renderer_fill(renderer);
        rr_renderer_stroke(renderer);
    }
    // the flag points are rings, the flags are filled
    rr_renderer_set_line_width(renderer, 2);
    for (uint8_t t = 0; t < c->team_count; ++t)
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        {
            if (!c->teams[t].point_set[i])
                continue;
            rr_renderer_set_stroke(renderer, rr_ctf_team_color(t));
            rr_renderer_begin_path(renderer);
            rr_renderer_arc(
                renderer, size * (c->teams[t].point_x[i] / world_size - 0.5f),
                size * (c->teams[t].point_y[i] / world_size - 0.5f), 6);
            rr_renderer_stroke(renderer);
        }
    if (!c->flags_ready)
        return;
    for (uint8_t f = 0; f < c->team_count * RR_CTF_FLAGS_PER_TEAM; ++f)
    {
        struct rr_game_ctf_flag *flag = &c->flags[f];
        if (flag->state == rr_ctf_flag_banked)
            continue;
        rr_renderer_set_fill(renderer, rr_ctf_team_color(f / RR_CTF_FLAGS_PER_TEAM));
        rr_renderer_set_stroke(renderer, rr_ctf_team_color(flag->holder));
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, size * (flag->lerp_x / world_size - 0.5f),
                        size * (flag->lerp_y / world_size - 0.5f), 4.5f);
        rr_renderer_fill(renderer);
        rr_renderer_stroke(renderer);
    }
}
