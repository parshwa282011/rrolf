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

#include <string.h>

// The rules of capture the flag, see the comment in Server/Ctf.h. Nothing here
// touches the server, so tests can drive it with made-up players.

static struct rr_ctf_flag *flag_of(struct rr_ctf_match *this, uint8_t owner,
                                   uint8_t index)
{
    return &this->flags[owner * RR_CTF_FLAGS_PER_TEAM + index];
}

static uint8_t flag_id(struct rr_ctf_match *this, struct rr_ctf_flag *flag)
{
    return flag - this->flags;
}

// flags are stored by owner, so the slot says whether the flag is in the match.
// The slots of teams that are not playing are all zero, their owner field is
// not to be trusted
static uint8_t flag_in_match(struct rr_ctf_match *this, uint8_t id)
{
    return this->teams[id / RR_CTF_FLAGS_PER_TEAM].active;
}

void rr_ctf_match_reset(struct rr_ctf_match *this)
{
    memset(this, 0, sizeof *this);
    this->winner = RR_CTF_NONE;
    this->host_squad = RR_CTF_NONE;
    memset(this->squad_team, RR_CTF_NONE, sizeof this->squad_team);
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        this->teams[t].squad = RR_CTF_NONE;
        this->teams[t].last_taker = RR_CTF_NONE;
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            this->teams[t].point_flag[i] = -1;
    }
}

void rr_ctf_push_event(struct rr_ctf_match *this, uint8_t type, uint8_t a,
                       uint8_t b, uint8_t flag)
{
    memmove(&this->events[0], &this->events[1],
            sizeof this->events[0] * (RR_CTF_EVENT_HISTORY - 1));
    this->events[RR_CTF_EVENT_HISTORY - 1] =
        (struct rr_ctf_event){type, a, b, flag};
    ++this->event_seq;
}

void rr_ctf_set_point(struct rr_ctf_match *this, uint8_t team, uint8_t index,
                      float x, float y)
{
    struct rr_ctf_team *t = &this->teams[team];
    t->point_set[index] = 1;
    t->point_x[index] = x;
    t->point_y[index] = y;
}

uint8_t rr_ctf_all_points_set(struct rr_ctf_match *this)
{
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        if (!this->teams[t].active)
            continue;
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
            if (!this->teams[t].point_set[i])
                return 0;
    }
    return 1;
}

void rr_ctf_create_flags(struct rr_ctf_match *this)
{
    for (uint8_t t = 0; t < RR_CTF_MAX_TEAMS; ++t)
    {
        struct rr_ctf_team *team = &this->teams[t];
        if (!team->active)
            continue;
        for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        {
            struct rr_ctf_flag *flag = flag_of(this, t, i);
            memset(flag, 0, sizeof *flag);
            flag->owner = flag->holder = t;
            flag->index = flag->point = flag->home_point = i;
            flag->state = rr_ctf_flag_standing;
            flag->carrier = -1;
            flag->x = team->point_x[i];
            flag->y = team->point_y[i];
            team->point_flag[i] = flag_id(this, flag);
        }
    }
    this->flags_ready = 1;
}

uint8_t rr_ctf_held(struct rr_ctf_match *this, uint8_t team)
{
    uint8_t n = 0;
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
        if (flag_in_match(this, f) && this->flags[f].holder == team)
            ++n;
    return n;
}

// the flags of other teams that stand on the team's points or are banked
uint8_t rr_ctf_held_foreign(struct rr_ctf_match *this, uint8_t team)
{
    uint8_t n = 0;
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
    {
        struct rr_ctf_flag *flag = &this->flags[f];
        if (!flag_in_match(this, f) || flag->holder != team ||
            flag->owner == team)
            continue;
        if (flag->state == rr_ctf_flag_standing ||
            flag->state == rr_ctf_flag_banked)
            ++n;
    }
    return n;
}

uint8_t rr_ctf_banked(struct rr_ctf_match *this, uint8_t team)
{
    uint8_t n = 0;
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
        if (flag_in_match(this, f) && this->flags[f].holder == team &&
            this->flags[f].state == rr_ctf_flag_banked)
            ++n;
    return n;
}

int8_t rr_ctf_carried_by(struct rr_ctf_match *this, uint8_t player)
{
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
        if (flag_in_match(this, f) &&
            this->flags[f].state == rr_ctf_flag_carried &&
            this->flags[f].carrier == (int8_t)player)
            return f;
    return -1;
}

static int8_t empty_point(struct rr_ctf_match *this, uint8_t team)
{
    for (uint8_t i = 0; i < RR_CTF_FLAGS_PER_TEAM; ++i)
        if (this->teams[team].point_flag[i] == -1)
            return i;
    return -1;
}

static void stand_on_point(struct rr_ctf_match *this, struct rr_ctf_flag *flag,
                           uint8_t team, uint8_t point)
{
    struct rr_ctf_team *t = &this->teams[team];
    flag->state = rr_ctf_flag_standing;
    flag->holder = team;
    flag->point = point;
    flag->carrier = -1;
    flag->x = t->point_x[point];
    flag->y = t->point_y[point];
    t->point_flag[point] = flag_id(this, flag);
    t->setup_ticks[point] = 0;
}

static void bank_flag(struct rr_ctf_flag *flag, uint8_t team)
{
    flag->state = rr_ctf_flag_banked;
    flag->holder = team;
    flag->carrier = -1;
}

// a flag that is on the ground goes back to where it came from
static void send_home(struct rr_ctf_match *this, struct rr_ctf_flag *flag)
{
    uint8_t holder = flag->holder;
    rr_ctf_push_event(this, rr_ctf_event_returned, holder, flag->owner,
                      flag_id(this, flag));
    struct rr_ctf_team *t = &this->teams[holder];
    if (t->point_flag[flag->home_point] == -1)
        stand_on_point(this, flag, holder, flag->home_point);
    else
    {
        // the point was set up again with another flag meanwhile
        int8_t point = empty_point(this, holder);
        if (point >= 0)
            stand_on_point(this, flag, holder, point);
        else
            bank_flag(flag, holder);
    }
}

static void take_flag(struct rr_ctf_match *this, struct rr_ctf_flag *flag,
                      uint8_t player, uint8_t team)
{
    struct rr_ctf_team *from = &this->teams[flag->holder];
    if (flag->state == rr_ctf_flag_standing)
    {
        flag->home_point = flag->point;
        from->point_flag[flag->point] = -1;
        from->setup_ticks[flag->point] = 0;
    }
    flag->state = rr_ctf_flag_carried;
    flag->carrier = player;
    flag->return_ticks = 0;
    rr_ctf_push_event(this, rr_ctf_event_taken, team, flag->holder,
                      flag_id(this, flag));
}

static void drop_flag(struct rr_ctf_match *this, struct rr_ctf_flag *flag,
                      uint8_t carrier_team)
{
    flag->state = rr_ctf_flag_dropped;
    flag->carrier = -1;
    flag->return_ticks = RR_CTF_RETURN_TICKS;
    rr_ctf_push_event(this, rr_ctf_event_dropped, carrier_team, flag->holder,
                      flag_id(this, flag));
}

static float distance_squared(float x0, float y0, float x1, float y1)
{
    return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1);
}

void rr_ctf_eliminate(struct rr_ctf_match *this, uint8_t team, uint8_t by)
{
    struct rr_ctf_team *t = &this->teams[team];
    if (t->eliminated)
        return;
    t->eliminated = 1;
    if (by == RR_CTF_NONE || by == team || !this->teams[by].active ||
        this->teams[by].eliminated)
    {
        // whoever holds the most
        by = RR_CTF_NONE;
        uint8_t best = 0;
        for (uint8_t o = 0; o < RR_CTF_MAX_TEAMS; ++o)
            if (o != team && this->teams[o].active && !this->teams[o].eliminated &&
                (by == RR_CTF_NONE || rr_ctf_held(this, o) > best))
            {
                by = o;
                best = rr_ctf_held(this, o);
            }
    }
    // what the team had banked goes to the team that finished it. Its own
    // flags that still stand stay where they are, for anyone to take
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
    {
        struct rr_ctf_flag *flag = &this->flags[f];
        if (flag->holder == team && flag->state == rr_ctf_flag_banked &&
            by != RR_CTF_NONE)
            flag->holder = by;
    }
    rr_ctf_push_event(this, rr_ctf_event_eliminated, team, by, 0);
}

void rr_ctf_step(struct rr_ctf_match *this, struct rr_ctf_player *players)
{
    if (!this->flags_ready)
        return;
    // flags that are carried follow the carrier, a carrier that is gone or dead
    // drops it where it was
    for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
    {
        struct rr_ctf_flag *flag = &this->flags[f];
        if (!flag_in_match(this, f))
            continue;
        if (flag->state == rr_ctf_flag_carried)
        {
            struct rr_ctf_player *p = &players[(uint8_t)flag->carrier];
            if (p->present && p->alive &&
                !this->teams[p->team].eliminated)
            {
                flag->x = p->x;
                flag->y = p->y;
            }
            else
            {
                // the flag falls where the carrier went down
                if (p->located)
                {
                    flag->x = p->x;
                    flag->y = p->y;
                }
                drop_flag(this, flag, p->team);
            }
        }
        else if (flag->state == rr_ctf_flag_dropped)
        {
            if (flag->return_ticks > 0)
                --flag->return_ticks;
            if (flag->return_ticks == 0)
                send_home(this, flag);
        }
    }
    // what the players are doing
    uint8_t present_at_point[RR_CTF_MAX_TEAMS][RR_CTF_FLAGS_PER_TEAM] = {{0}};
    for (uint8_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        struct rr_ctf_player *p = &players[i];
        if (!p->present || !p->alive || p->team >= RR_CTF_MAX_TEAMS ||
            !this->teams[p->team].active || this->teams[p->team].eliminated)
            continue;
        uint8_t team = p->team;
        struct rr_ctf_team *t = &this->teams[team];
        int8_t carrying = rr_ctf_carried_by(this, i);
        if (carrying >= 0)
        {
            // bank it at any of the team's flag points
            for (uint8_t pt = 0; pt < RR_CTF_FLAGS_PER_TEAM; ++pt)
            {
                if (distance_squared(p->x, p->y, t->point_x[pt],
                                     t->point_y[pt]) >
                    RR_CTF_BANK_RADIUS * RR_CTF_BANK_RADIUS)
                    continue;
                struct rr_ctf_flag *flag = &this->flags[carrying];
                uint8_t from = flag->holder;
                this->teams[from].last_taker = team;
                bank_flag(flag, team);
                rr_ctf_push_event(this, rr_ctf_event_banked, team, from,
                                  carrying);
                carrying = -1;
                break;
            }
        }
        else
        {
            // take a flag, or send a dropped flag of the team home
            for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
            {
                struct rr_ctf_flag *flag = &this->flags[f];
                if (!flag_in_match(this, f) ||
                    (flag->state != rr_ctf_flag_standing &&
                     flag->state != rr_ctf_flag_dropped) ||
                    distance_squared(p->x, p->y, flag->x, flag->y) >
                        RR_CTF_PICKUP_RADIUS * RR_CTF_PICKUP_RADIUS)
                    continue;
                if (flag->holder == team)
                {
                    if (flag->state == rr_ctf_flag_dropped)
                        send_home(this, flag);
                    continue;
                }
                take_flag(this, flag, i, team);
                break;
            }
        }
        // standing on an empty flag point of the team, with a banked flag to
        // put there
        if (rr_ctf_banked(this, team) > 0)
            for (uint8_t pt = 0; pt < RR_CTF_FLAGS_PER_TEAM; ++pt)
                if (t->point_flag[pt] == -1 &&
                    distance_squared(p->x, p->y, t->point_x[pt],
                                     t->point_y[pt]) <=
                        RR_CTF_SETUP_RADIUS * RR_CTF_SETUP_RADIUS)
                    present_at_point[team][pt] = 1;
    }
    for (uint8_t team = 0; team < RR_CTF_MAX_TEAMS; ++team)
    {
        struct rr_ctf_team *t = &this->teams[team];
        if (!t->active || t->eliminated)
            continue;
        for (uint8_t pt = 0; pt < RR_CTF_FLAGS_PER_TEAM; ++pt)
        {
            if (t->point_flag[pt] != -1 || rr_ctf_banked(this, team) == 0)
            {
                t->setup_ticks[pt] = 0;
                continue;
            }
            if (!present_at_point[team][pt])
            {
                if (t->setup_ticks[pt] > 0)
                    --t->setup_ticks[pt];
                continue;
            }
            if (++t->setup_ticks[pt] < RR_CTF_SETUP_TICKS)
                continue;
            for (uint8_t f = 0; f < RR_CTF_FLAG_COUNT; ++f)
            {
                struct rr_ctf_flag *flag = &this->flags[f];
                if (flag->state != rr_ctf_flag_banked || flag->holder != team ||
                    !flag_in_match(this, f))
                    continue;
                stand_on_point(this, flag, team, pt);
                rr_ctf_push_event(this, rr_ctf_event_setup, team, flag->owner,
                                  f);
                break;
            }
        }
    }
    // a team that holds too few flags is out, then someone may have won.
    // Eliminating a team can leave another under the limit (its banked flags
    // move), so go on until nothing changes
    uint8_t changed = 1;
    while (changed)
    {
        changed = 0;
        for (uint8_t team = 0; team < RR_CTF_MAX_TEAMS; ++team)
        {
            struct rr_ctf_team *t = &this->teams[team];
            if (!t->active || t->eliminated ||
                rr_ctf_held(this, team) >= RR_CTF_MIN_FLAGS)
                continue;
            rr_ctf_eliminate(this, team, t->last_taker);
            changed = 1;
        }
    }
    if (this->winner != RR_CTF_NONE)
        return;
    for (uint8_t team = 0; team < RR_CTF_MAX_TEAMS; ++team)
    {
        struct rr_ctf_team *t = &this->teams[team];
        if (!t->active || t->eliminated)
            continue;
        if (rr_ctf_held_foreign(this, team) ==
            RR_CTF_FLAGS_PER_TEAM * (this->team_count - 1))
        {
            this->winner = team;
            rr_ctf_push_event(this, rr_ctf_event_won, team, RR_CTF_NONE, 0);
            return;
        }
    }
}
