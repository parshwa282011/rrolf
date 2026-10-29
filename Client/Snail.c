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

#include <Client/Snail.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Client/Assets/Render.h>
#include <Client/Game.h>
#include <Client/Renderer/Renderer.h>
#include <Shared/Snail.h>
#include <Shared/StaticData.h>
#include <Shared/pb.h>

uint8_t rr_snail_client_running(struct rr_game *this)
{
    return this->snail.active && this->snail.run_count > 0;
}

void rr_snail_client_reset(struct rr_game *this)
{
    memset(&this->snail, 0, sizeof this->snail);
}

static void add_feed_line(struct rr_game *this, char const *text,
                          uint32_t color)
{
    struct rr_game_snail *s = &this->snail;
    memmove(&s->feed[1], &s->feed[0], sizeof s->feed[0] * 2);
    snprintf(s->feed[0].text, sizeof s->feed[0].text, "%s", text);
    s->feed[0].color = color;
    s->feed[0].age = 0;
}

static void show_event(struct rr_game *this, struct rr_game_snail_event *e)
{
    char text[96];
    uint32_t color = RR_RARITY_COLORS[e->rarity < rr_rarity_id_max ? e->rarity : 0];
    switch (e->type)
    {
    case rr_snail_event_started:
        snprintf(text, sizeof text, "A %s snail is stuck far away. Find it!",
                 RR_RARITY_NAMES[e->rarity]);
        break;
    case rr_snail_event_arrived:
        snprintf(text, sizeof text, "The snail made it home! %u rewarded",
                 e->count);
        color = 0xff7ee0b0;
        break;
    case rr_snail_event_eaten:
        snprintf(text, sizeof text, "The snail was eaten");
        color = 0xffff6060;
        break;
    case rr_snail_event_lost:
        snprintf(text, sizeof text, "The snail got lost");
        color = 0xffdddddd;
        break;
    default:
        return;
    }
    add_feed_line(this, text, color);
}

void rr_snail_client_read_state(struct rr_game *this, struct proto_bug *d)
{
    struct rr_game_snail *s = &this->snail;
    s->active = 1;
    uint16_t seq = proto_bug_read_varuint(d, "snail event seq");
    struct rr_game_snail_event events[RR_SNAIL_EVENT_HISTORY];
    for (uint8_t e = 0; e < RR_SNAIL_EVENT_HISTORY; ++e)
    {
        events[e].type = proto_bug_read_uint8(d, "snail event");
        events[e].rarity = proto_bug_read_uint8(d, "snail event r");
        events[e].count = proto_bug_read_uint8(d, "snail event n");
    }
    // events newer than the last packet become lines in the feed. A client who
    // just joined does not get the old ones
    if (!s->event_seq_known)
    {
        s->event_seq_known = 1;
        s->event_seq = seq;
    }
    else if (seq != s->event_seq)
    {
        uint16_t fresh = (uint16_t)(seq - s->event_seq);
        if (fresh > RR_SNAIL_EVENT_HISTORY)
            fresh = RR_SNAIL_EVENT_HISTORY;
        for (uint16_t k = RR_SNAIL_EVENT_HISTORY - fresh;
             k < RR_SNAIL_EVENT_HISTORY; ++k)
            show_event(this, &events[k]);
        s->event_seq = seq;
    }
    s->run_count = proto_bug_read_uint8(d, "snail runs");
    if (s->run_count > RR_SNAIL_MAX_RUNS)
        s->run_count = RR_SNAIL_MAX_RUNS;
    if (s->run_count == 0)
        return;
    s->hole_x = proto_bug_read_float32(d, "hole x");
    s->hole_y = proto_bug_read_float32(d, "hole y");
    for (uint8_t i = 0; i < s->run_count; ++i)
    {
        struct rr_game_snail_run *r = &s->runs[i];
        r->rarity = proto_bug_read_uint8(d, "snail rarity");
        r->route_cells = proto_bug_read_varuint(d, "snail route");
        r->cells_left = proto_bug_read_varuint(d, "snail left");
        r->hp = proto_bug_read_uint8(d, "snail hp");
        r->waiting = proto_bug_read_uint8(d, "snail waiting");
        r->x = proto_bug_read_float32(d, "snail x");
        r->y = proto_bug_read_float32(d, "snail y");
    }
}

// the snail closest to the player, the one the banner is about
struct rr_game_snail_run *rr_snail_client_focus(struct rr_game *this)
{
    struct rr_game_snail *s = &this->snail;
    struct rr_game_snail_run *best = NULL;
    float best_distance = 1e30f;
    for (uint8_t i = 0; i < s->run_count; ++i)
    {
        float dx = s->runs[i].x, dy = s->runs[i].y;
        if (this->player_info != NULL)
        {
            dx -= this->player_info->lerp_camera_x;
            dy -= this->player_info->lerp_camera_y;
        }
        if (dx * dx + dy * dy < best_distance)
        {
            best_distance = dx * dx + dy * dy;
            best = &s->runs[i];
        }
    }
    return best;
}

void rr_snail_client_tick(struct rr_game *this, float delta)
{
    struct rr_game_snail *s = &this->snail;
    s->anim += delta;
    for (uint8_t i = 0; i < 3; ++i)
        if (s->feed[i].text[0] != 0)
            s->feed[i].age += delta;
}

void rr_snail_client_render_world(struct rr_game *this)
{
    struct rr_game_snail *s = &this->snail;
    if (!rr_snail_client_running(this) || this->player_info == NULL ||
        this->player_info->arena != 1)
        return;
    struct rr_renderer *renderer = this->renderer;
    struct rr_renderer_context_state state;
    rr_renderer_context_state_init(renderer, &state);
    rr_renderer_translate(renderer, s->hole_x, s->hole_y);
    rr_renderer_scale(renderer, 0.45f);
    rr_snail_hole_draw(renderer);
    // a pulse so it is easy to find
    float pulse = 0.5f + 0.5f * sinf(s->anim * 3);
    rr_renderer_set_stroke(renderer, 0xff7ee0b0);
    rr_renderer_set_line_width(renderer, 10);
    rr_renderer_set_global_alpha(renderer, 0.6f * (1 - pulse));
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 0, 170 + 120 * pulse);
    rr_renderer_stroke(renderer);
    rr_renderer_set_global_alpha(renderer, 1);
    rr_renderer_context_state_free(renderer, &state);
}

void rr_snail_minimap_overlay(struct rr_game *this, float size, float view_x,
                              float view_y, float span)
{
    struct rr_game_snail *s = &this->snail;
    if (!rr_snail_client_running(this))
        return;
    struct rr_renderer *renderer = this->renderer;
    float to_px = size / span;
    float half = size / 2;
    // the hole is a ring, the snail a filled dot. A pulse makes them findable
    float pulse = 0.5f + 0.5f * sinf(s->anim * 4);
    float hx = rr_fclamp((s->hole_x - view_x) * to_px, -half, half);
    float hy = rr_fclamp((s->hole_y - view_y) * to_px, -half, half);
    rr_renderer_set_stroke(renderer, 0xff7ee0b0);
    rr_renderer_set_line_width(renderer, 2.5f);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, hx, hy, 5 + 2 * pulse);
    rr_renderer_stroke(renderer);
    rr_renderer_set_fill(renderer, 0xffd88a38);
    rr_renderer_set_stroke(renderer, 0xff222222);
    rr_renderer_set_line_width(renderer, 1.5f);
    for (uint8_t i = 0; i < s->run_count; ++i)
    {
        float sx = rr_fclamp((s->runs[i].x - view_x) * to_px, -half, half);
        float sy = rr_fclamp((s->runs[i].y - view_y) * to_px, -half, half);
        rr_renderer_begin_path(renderer);
        rr_renderer_arc(renderer, sx, sy, 4);
        rr_renderer_fill(renderer);
        rr_renderer_stroke(renderer);
    }
}
