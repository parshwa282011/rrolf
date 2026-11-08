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

#include <Server/Snail.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Server/EntityAllocation.h>
#include <Server/Server.h>
#include <Server/Simulation.h>
#include <Server/SpatialHash.h>
#include <Shared/Component/Arena.h>
#include <Shared/Component/Drop.h>
#include <Shared/Component/Flower.h>
#include <Shared/Component/Mob.h>
#include <Shared/Component/Physical.h>
#include <Shared/Component/Relations.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>
#include <Shared/pb.h>

// where the snail hole is, in template cells (tools/garden/check-map.py checks
// that it and every start are open and how far each start is)
#define HOLE_X 20
#define HOLE_Y 21

#define UNREACHABLE 0xffffffffu

#define FIRST_RUN_TICKS (3 * 60 * 25)
#define RUN_PERIOD_TICKS (10 * 60 * 25)
#define RESULT_TICKS (15 * 25)
// no player in the game for this long and the snail is given up on
#define QUIET_LIMIT (30 * 25)
#define STUCK_LIMIT (40 * 25)
// nobody came to wake the snail for this long and it is given up on
#define WAIT_LIMIT (8 * 60 * 25)

// settings for the automated test in tools/garden, which can't wait ten
// minutes. The deployed server never sets these
static uint32_t run_period_ticks = RUN_PERIOD_TICKS;
static int forced_rarity = -1;
static float speed_scale = 1;

static float cell_size() { return 2 * RR_MAZES[rr_biome_id_garden].grid_size; }

// how fast a snail moves: a bit under half a player's speed, a bit more for
// each rarity so the harder runs are not a stroll. In units per tick
static float snail_accel(uint8_t rarity)
{
    return RR_PLAYER_SPEED * (0.45f + 0.03f * rarity) * speed_scale;
}

static float snail_speed(uint8_t rarity)
{
    return snail_accel(rarity) * 4; // friction 0.75, see system_velocity
}

static void push_event(struct rr_snail *s, uint8_t type, uint8_t rarity,
                       uint8_t count)
{
    memmove(&s->events[0], &s->events[1],
            sizeof s->events[0] * (RR_SNAIL_EVENT_HISTORY - 1));
    s->events[RR_SNAIL_EVENT_HISTORY - 1].type = type;
    s->events[RR_SNAIL_EVENT_HISTORY - 1].rarity = rarity;
    s->events[RR_SNAIL_EVENT_HISTORY - 1].count = count;
    ++s->event_seq;
}

// ---- the way to the hole

// the template difficulty a snail of a rarity starts in, see StaticData.h
static void start_zone(uint8_t rarity, uint8_t *min, uint8_t *max)
{
    *min = RR_SNAIL_ZONE_MIN(RR_SNAIL_START_RARITY(rarity));
    *max = RR_SNAIL_ZONE_MAX(RR_SNAIL_START_RARITY(rarity));
}

// a random template cell in the zone of a rarity that can reach the hole, 0xffff
// if there is none
static uint16_t pick_start(struct rr_snail *s, uint8_t rarity)
{
    uint8_t min, max;
    start_zone(rarity, &min, &max);
    uint16_t chosen = 0xffff;
    uint32_t seen = 0;
    for (uint16_t at = 0; at < RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM;
         ++at)
    {
        uint8_t difficulty = RR_MAZE_TEMPLATE_GARDEN[at / RR_SNAIL_TEMPLATE_DIM]
                                                    [at % RR_SNAIL_TEMPLATE_DIM];
        if (s->cost[at] == UNREACHABLE || difficulty < min || difficulty > max)
            continue;
        // reservoir sampling: each cell ends up chosen with the same chance
        if (rand() % ++seen == 0)
            chosen = at;
    }
    return chosen;
}

// The cheapest way home from every open template cell: Dijkstra outwards from the hole.
// A step into a cell costs RR_SNAIL_CELL_COST of its difficulty, so the snail prefers
// the easier parts of the map even when they are a longer way round. 1600 cells, so
// picking the cheapest open one by looking at all of them is fast enough, once
static uint32_t cell_cost(uint16_t at)
{
    return RR_SNAIL_CELL_COST(RR_MAZE_TEMPLATE_GARDEN[at / RR_SNAIL_TEMPLATE_DIM]
                                                     [at % RR_SNAIL_TEMPLATE_DIM]);
}

static void build_field(struct rr_snail *s)
{
    uint32_t const cells = RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM;
    static uint8_t done[RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM];
    for (uint32_t i = 0; i < cells; ++i)
    {
        s->cost[i] = UNREACHABLE;
        s->steps[i] = 255;
        done[i] = 0;
    }
    s->cost[HOLE_Y * RR_SNAIL_TEMPLATE_DIM + HOLE_X] = 0;
    s->steps[HOLE_Y * RR_SNAIL_TEMPLATE_DIM + HOLE_X] = 0;
    for (;;)
    {
        uint32_t best = UNREACHABLE;
        uint16_t at = 0xffff;
        for (uint16_t i = 0; i < cells; ++i)
            if (!done[i] && s->cost[i] < best)
            {
                best = s->cost[i];
                at = i;
            }
        if (at == 0xffff)
            break;
        done[at] = 1;
        int32_t x = at % RR_SNAIL_TEMPLATE_DIM, y = at / RR_SNAIL_TEMPLATE_DIM;
        static int8_t const step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (uint8_t i = 0; i < 4; ++i)
        {
            int32_t nx = x + step[i][0], ny = y + step[i][1];
            if (nx < 0 || ny < 0 || nx >= RR_SNAIL_TEMPLATE_DIM ||
                ny >= RR_SNAIL_TEMPLATE_DIM)
                continue;
            if (RR_MAZE_TEMPLATE_GARDEN[ny][nx] == 0)
                continue;
            uint16_t n = ny * RR_SNAIL_TEMPLATE_DIM + nx;
            // from n the snail steps into at
            uint32_t through = s->cost[at] + cell_cost(at);
            if (!done[n] && through < s->cost[n] && s->steps[at] < 254)
            {
                s->cost[n] = through;
                s->steps[n] = s->steps[at] + 1;
            }
        }
    }
    s->hole_x = (HOLE_X + 0.5f) * cell_size();
    s->hole_y = (HOLE_Y + 0.5f) * cell_size();
    // every rarity needs somewhere to start
    s->field_ready = 1;
    for (uint8_t rarity = RR_SNAIL_MIN_RARITY; rarity < rr_rarity_id_max;
         ++rarity)
        if (pick_start(s, rarity) == 0xffff)
            s->field_ready = 0;
}

// the centre of the neighbouring cell on the cheapest way to the hole, or the hole
static void next_waypoint(struct rr_snail *s, float x, float y, float *tx,
                          float *ty, int32_t *cell_out, uint16_t *left_out)
{
    int32_t cx = x / cell_size(), cy = y / cell_size();
    *tx = s->hole_x;
    *ty = s->hole_y;
    *cell_out = -1;
    *left_out = 0;
    if (cx < 0 || cy < 0 || cx >= RR_SNAIL_TEMPLATE_DIM ||
        cy >= RR_SNAIL_TEMPLATE_DIM)
        return;
    uint16_t here = cy * RR_SNAIL_TEMPLATE_DIM + cx;
    *cell_out = here;
    if (s->cost[here] == UNREACHABLE)
        return; // knocked into a wall, head straight for the hole
    *left_out = s->steps[here];
    if (s->cost[here] == 0)
        return;
    static int8_t const step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    uint32_t best = UNREACHABLE;
    for (uint8_t i = 0; i < 4; ++i)
    {
        int32_t nx = cx + step[i][0], ny = cy + step[i][1];
        if (nx < 0 || ny < 0 || nx >= RR_SNAIL_TEMPLATE_DIM ||
            ny >= RR_SNAIL_TEMPLATE_DIM)
            continue;
        uint16_t n = ny * RR_SNAIL_TEMPLATE_DIM + nx;
        if (s->cost[n] == UNREACHABLE)
            continue;
        uint32_t through = s->cost[n] + cell_cost(n);
        if (through < best)
        {
            best = through;
            *tx = (nx + 0.5f) * cell_size();
            *ty = (ny + 0.5f) * cell_size();
        }
    }
}

// ---- the run

// players who are playing: connected and with a flower, alive or not. A player
// who is dead is about to respawn, and the run waits for them
static uint32_t players_in_game(struct rr_server *this)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        if (!rr_bitset_get(this->clients_in_use, i))
            continue;
        struct rr_server_client *client = &this->clients[i];
        if (client->disconnected || client->player_info == NULL ||
            client->player_info->flower_id == RR_NULL_ENTITY)
            continue;
        if (rr_simulation_has_flower(&this->simulation,
                                     client->player_info->flower_id))
            ++count;
    }
    return count;
}

static void remove_snail(struct rr_server *this, struct rr_snail_run *r)
{
    if (r->snail != RR_NULL_ENTITY &&
        rr_simulation_entity_alive(&this->simulation, r->snail))
    {
        rr_simulation_get_mob(&this->simulation, r->snail_id)->no_drop = 1;
        rr_simulation_request_entity_deletion(&this->simulation, r->snail_id);
    }
    r->snail = RR_NULL_ENTITY;
}

// the closest a player in the game is to a point, squared. Dead players count
// too, they are about to be back
static float player_distance_sq(struct rr_server *this, float x, float y)
{
    float best = 1e18f;
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
    {
        if (!rr_bitset_get(this->clients_in_use, i))
            continue;
        struct rr_server_client *client = &this->clients[i];
        if (client->disconnected || client->player_info == NULL ||
            client->player_info->flower_id == RR_NULL_ENTITY ||
            !rr_simulation_has_flower(&this->simulation,
                                      client->player_info->flower_id))
            continue;
        struct rr_component_physical *physical = rr_simulation_get_physical(
            &this->simulation, client->player_info->flower_id);
        if (physical->arena != 1)
            continue;
        float dx = physical->x - x, dy = physical->y - y;
        if (dx * dx + dy * dy < best)
            best = dx * dx + dy * dy;
    }
    return best;
}

static uint8_t roll_rarity()
{
    float total = 0;
    for (uint8_t r = 0; r < rr_rarity_id_max; ++r)
        total += RR_SNAIL_RARITY_WEIGHTS[r];
    float seed = rr_frand() * total;
    for (uint8_t r = 0; r < rr_rarity_id_max; ++r)
    {
        if (seed < RR_SNAIL_RARITY_WEIGHTS[r])
            return r;
        seed -= RR_SNAIL_RARITY_WEIGHTS[r];
    }
    return RR_SNAIL_MIN_RARITY;
}

static void start_run(struct rr_server *this, struct rr_snail_run *r,
                      uint8_t rarity)
{
    struct rr_snail *s = &this->snail;
    if (rarity < RR_SNAIL_MIN_RARITY)
        rarity = RR_SNAIL_MIN_RARITY;
    uint16_t start = pick_start(s, rarity);
    uint8_t sx = start % RR_SNAIL_TEMPLATE_DIM, sy = start / RR_SNAIL_TEMPLATE_DIM;
    r->rarity = rarity;
    r->route_cells = s->steps[sy * RR_SNAIL_TEMPLATE_DIM + sx];
    r->cells_left = r->route_cells;
    float x = (sx + 0.5f) * cell_size(), y = (sy + 0.5f) * cell_size();
    EntityIdx e = rr_simulation_alloc_mob(&this->simulation, 1, x, y,
                                          rr_mob_id_snail, rarity,
                                          rr_simulation_team_id_players);
    rr_simulation_get_mob(&this->simulation, e)->no_drop = 1;
    if (rarity < rr_rarity_id_ancient)
    {
        // every snail below ancient has twice the health
        struct rr_component_health *health =
            rr_simulation_get_health(&this->simulation, e);
        rr_component_health_set_max_health(health, health->max_health * 2);
        rr_component_health_set_health(health, health->max_health);
    }
    r->snail_id = e;
    r->snail = rr_simulation_get_entity_hash(&this->simulation, e);
    r->snail_x = x;
    r->snail_y = y;
    r->hp_percent = 100;
    r->ticks_running = 0;
    r->waiting = 1;
    r->waiting_ticks = 0;
    r->wait_x = x;
    r->wait_y = y;
    rr_simulation_get_health(&this->simulation, e)->invulnerable = 1;
    // three times as long as it needs, in case it gets held up
    r->ticks_limit =
        3 * r->route_cells * cell_size() / snail_speed(rarity) + 25 * 60;
    r->quiet_ticks = r->stuck_ticks = 0;
    r->last_cell = -1;
    r->phase = rr_snail_phase_running;
    push_event(s, rr_snail_event_started, rarity, 0);
    fprintf(stderr,
            "<snail::started slot=%d rarity=%u route=%u at=%u,%u players=%u>\n",
            (int)(r - s->runs), rarity, r->route_cells, sx, sy,
            players_in_game(this));
}

// What a player is given: at most RR_SNAIL_MAX_REWARDS petals, which the snail turns
// into drops on the ground at the player's feet when it gets home (the same drops a
// mob leaves: the player walks over them, only they can pick them up)
#define RR_SNAIL_MAX_REWARDS 12

struct rewards
{
    uint8_t count;
    uint8_t id[RR_SNAIL_MAX_REWARDS];
    uint8_t rarity[RR_SNAIL_MAX_REWARDS];
};

static void give(struct rewards *rewards, uint8_t id, uint8_t rarity)
{
    if (rewards->count >= RR_SNAIL_MAX_REWARDS)
        return;
    rewards->id[rewards->count] = id;
    rewards->rarity[rewards->count] = rarity;
    ++rewards->count;
}

// exactly one, its rarity rolled from RR_SNAIL_REWARD_TIER_CHANCE. Not below the
// lowest rarity the petal exists at
static void give_reward_petal(struct rewards *rewards, uint8_t id, uint8_t rarity)
{
    float roll = rr_frand() * 100;
    uint8_t below = 3;
    for (uint8_t i = 0; i < 4; ++i)
    {
        roll -= RR_SNAIL_REWARD_TIER_CHANCE[rarity][i];
        if (roll < 0)
        {
            below = i;
            break;
        }
    }
    int given = rarity - below;
    if (given < (int)RR_PETAL_DATA[id].min_rarity)
        given = RR_PETAL_DATA[id].min_rarity;
    give(rewards, id, (uint8_t)given);
}

// a drop on the ground that only one player can pick up, like the ones mobs leave
// (Shared/Component/Mob.c)
static void drop_for(struct rr_server *this, struct rr_server_client *client,
                     struct rr_component_physical *at, uint8_t id,
                     uint8_t rarity, uint8_t index, uint8_t count)
{
    struct rr_simulation *simulation = &this->simulation;
    EntityIdx entity = rr_simulation_alloc_entity(simulation);
    struct rr_component_physical *physical =
        rr_simulation_add_physical(simulation, entity);
    struct rr_component_drop *drop = rr_simulation_add_drop(simulation, entity);
    struct rr_component_relations *relations =
        rr_simulation_add_relations(simulation, entity);
    rr_component_physical_set_x(physical, at->x);
    rr_component_physical_set_y(physical, at->y);
    rr_component_physical_set_radius(physical, 20);
    rr_component_drop_set_id(drop, id);
    rr_component_drop_set_rarity(drop, rarity);
    rr_component_relations_set_team(relations, rr_simulation_team_id_players);
    drop->ticks_until_despawn = 25 * 10 * (rarity + 1);
    rr_bitset_set(drop->can_be_picked_up_by, client - this->clients);
    physical->arena = at->arena;
    if (count != 1)
    {
        float angle = M_PI * 2 * (index + 0.65 * rr_frand()) / count;
        rr_vector_from_polar(&physical->velocity, 15 + 20 * rr_frand(), angle);
        physical->friction = 0.75;
    }
}

// the rewards of a snail of this rarity for one player: drops on the ground at `at`,
// only that player can pick them up. Returns how many
uint8_t rr_snail_give_rewards(struct rr_server *this,
                              struct rr_server_client *client,
                              struct rr_component_physical *at, uint8_t rarity)
{
    struct rewards rewards;
    rewards.count = 0;
    // nazars first, they are the ones worth the slots in the queue
    for (uint8_t q = 0; q < rr_rarity_id_max; ++q)
        if (rr_frand() * 100 < RR_SNAIL_NAZAR_CHANCE[rarity][q])
            give(&rewards, rr_petal_id_nazar, q);
    give_reward_petal(&rewards, rr_petal_id_shell, rarity);
    give_reward_petal(&rewards, rr_petal_id_mint, rarity);
    for (uint8_t i = 0; i < rewards.count; ++i)
        drop_for(this, client, at, rewards.id[i], rewards.rarity[i], i,
                 rewards.count);
    return rewards.count;
}

// Everybody in the game gets their own drops, wherever they are: every player who is
// connected and alive in the garden, not only the ones near the snail
static uint8_t reward_players(struct rr_server *this, struct rr_snail_run *r)
{
    uint8_t rewarded = 0;
    for (uint32_t squad = 0; squad < RR_SQUAD_COUNT; ++squad)
        for (uint8_t pos = 0; pos < RR_SQUAD_MEMBER_COUNT; ++pos)
        {
            struct rr_squad_member *member = &this->squads[squad].members[pos];
            if (member->in_use == 0 || member->client->disconnected ||
                member->client->verified == 0 ||
                member->client->player_info == NULL)
                continue;
            struct rr_component_player_info *player_info =
                member->client->player_info;
            EntityIdx flower = player_info->flower_id;
            if (flower == RR_NULL_ENTITY ||
                !rr_simulation_has_flower(&this->simulation, flower) ||
                is_dead_flower(&this->simulation, flower))
                continue;
            struct rr_component_physical *physical =
                rr_simulation_get_physical(&this->simulation, flower);
            if (physical->arena != 1)
                continue;
            rr_snail_give_rewards(this, member->client, physical, r->rarity);
            // whoever got the rewards has now seen this snail
            ++member->client->mob_gallery[rr_mob_id_snail][r->rarity];
            rr_server_client_write_to_api(member->client);
            rr_server_client_write_account(member->client);
            ++rewarded;
        }
    return rewarded;
}

static void finish(struct rr_server *this, struct rr_snail_run *r,
                   uint8_t phase, uint8_t type, uint8_t count)
{
    struct rr_snail *s = &this->snail;
    static char const *const names[] = {"none", "started", "arrived", "eaten",
                                        "lost"};
    fprintf(stderr,
            "<snail::%s slot=%d rarity=%u rewarded=%u cells_left=%u ticks=%u "
            "quiet=%u players=%u>\n",
            names[type], (int)(r - s->runs), r->rarity, count, r->cells_left,
            r->ticks_running, r->quiet_ticks, players_in_game(this));
    remove_snail(this, r);
    push_event(s, type, r->rarity, count);
    if (r == &s->runs[0])
    {
        r->phase = phase;
        r->phase_ticks = RESULT_TICKS;
    }
    else
        r->phase = rr_snail_phase_idle; // a dev snail's slot is free again
}

// While a snail sleeps no mob may be within RR_SNAIL_KEEP_OUT_RADIUS of it: the ones
// that are (they spawned there, wandered in, or were chasing a player) are put back
// on the edge of the circle. Mobs of the players (summons) are left alone
struct keep_out_captures
{
    struct rr_simulation *simulation;
    EntityIdx snail;
    float x;
    float y;
};

static void keep_out_cb(EntityIdx entity, void *_captures)
{
    struct keep_out_captures *captures = _captures;
    struct rr_simulation *simulation = captures->simulation;
    if (entity == captures->snail || !rr_simulation_has_mob(simulation, entity))
        return;
    struct rr_component_mob *mob = rr_simulation_get_mob(simulation, entity);
    if (mob->player_spawned || mob->id == rr_mob_id_snail)
        return;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, entity);
    float dx = physical->x - captures->x, dy = physical->y - captures->y;
    float distance_sq = dx * dx + dy * dy;
    float const limit = RR_SNAIL_KEEP_OUT_RADIUS;
    if (distance_sq >= limit * limit)
        return;
    if (distance_sq < 1)
    {
        float angle = rr_frand() * 2 * (float)M_PI;
        dx = cosf(angle);
        dy = sinf(angle);
        distance_sq = 1;
    }
    float scale = limit / sqrtf(distance_sq);
    rr_component_physical_set_x(physical, captures->x + dx * scale);
    rr_component_physical_set_y(physical, captures->y + dy * scale);
    rr_vector_set(&physical->velocity, 0, 0);
    rr_vector_set(&physical->collision_velocity, 0, 0);
}

static void keep_mobs_out(struct rr_server *this, EntityIdx snail_id,
                          struct rr_component_physical *snail)
{
    struct rr_simulation *simulation = &this->simulation;
    if (!rr_simulation_has_arena(simulation, snail->arena))
        return;
    struct rr_component_arena *arena =
        rr_simulation_get_arena(simulation, snail->arena);
    struct keep_out_captures captures = {simulation, snail_id,
                                         snail->x, snail->y};
    float const reach = RR_SNAIL_KEEP_OUT_RADIUS + 20;
    rr_spatial_hash_query(&arena->spatial_hash, snail->x, snail->y, reach, reach,
                          &captures, keep_out_cb);
}

static void tick_running(struct rr_server *this, struct rr_snail_run *r)
{
    struct rr_snail *s = &this->snail;
    struct rr_simulation *simulation = &this->simulation;
    if (!r->waiting)
        ++r->ticks_running;
    if (!rr_simulation_entity_alive(simulation, r->snail))
    {
        finish(this, r, rr_snail_phase_failed, rr_snail_event_eaten, 0);
        return;
    }
    if (players_in_game(this) == 0)
    {
        if (r->quiet_ticks == 0)
            fprintf(stderr, "<snail::nobody is playing, %u cells left>\n",
                    r->cells_left);
        if (++r->quiet_ticks > QUIET_LIMIT)
        {
            finish(this, r, rr_snail_phase_failed, rr_snail_event_lost, 0);
            return;
        }
    }
    else
        r->quiet_ticks = 0;
    if (r->ticks_running > r->ticks_limit)
    {
        finish(this, r, rr_snail_phase_failed, rr_snail_event_lost, 0);
        return;
    }

    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, r->snail_id);
    struct rr_component_health *health =
        rr_simulation_get_health(simulation, r->snail_id);
    r->snail_x = physical->x;
    r->snail_y = physical->y;
    r->hp_percent = 100 * health->health / health->max_health;

    if (r->waiting)
    {
        // the snail sleeps, nothing hurts it and mobs bumping into it don't move
        // it until somebody is right next to it. It loads nothing while it sleeps
        rr_component_physical_set_x(physical, r->wait_x);
        rr_component_physical_set_y(physical, r->wait_y);
        rr_vector_set(&physical->velocity, 0, 0);
        rr_vector_set(&physical->collision_velocity, 0, 0);
        keep_mobs_out(this, r->snail_id, physical);
        if (player_distance_sq(this, physical->x, physical->y) <
            (float)RR_SNAIL_START_RADIUS * RR_SNAIL_START_RADIUS)
        {
            r->waiting = 0;
            health->invulnerable = 0;
            fprintf(stderr, "<snail::woke slot=%d after %u ticks>\n",
                    (int)(r - s->runs), r->waiting_ticks);
        }
        else if (++r->waiting_ticks > WAIT_LIMIT)
            finish(this, r, rr_snail_phase_failed, rr_snail_event_lost, 0);
        return;
    }

    float dx = s->hole_x - physical->x, dy = s->hole_y - physical->y;
    if (dx * dx + dy * dy < 450.0f * 450.0f)
    {
        finish(this, r, rr_snail_phase_arrived, rr_snail_event_arrived,
               reward_players(this, r));
        return;
    }

    float tx, ty;
    int32_t cell;
    next_waypoint(s, physical->x, physical->y, &tx, &ty, &cell,
                  &r->cells_left);
    // held up in one cell for too long (a wall corner, a crowd): carry it on
    if (cell == r->last_cell)
    {
        if (++r->stuck_ticks > STUCK_LIMIT)
        {
            rr_component_physical_set_x(physical, tx);
            rr_component_physical_set_y(physical, ty);
            r->stuck_ticks = 0;
        }
    }
    else
    {
        r->last_cell = cell;
        r->stuck_ticks = 0;
    }
    struct rr_vector delta = {tx - physical->x, ty - physical->y};
    rr_component_physical_set_angle(
        physical, rr_angle_lerp(physical->angle, rr_vector_theta(&delta), 0.2));
    struct rr_vector accel;
    rr_vector_from_polar(&accel, snail_accel(r->rarity), physical->angle);
    rr_vector_add(&physical->acceleration, &accel);
}

void rr_snail_init(struct rr_server *this)
{
    struct rr_snail *s = &this->snail;
    memset(s, 0, sizeof *s);
    for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS; ++i)
        s->runs[i].snail = RR_NULL_ENTITY;
    s->runs[0].phase_ticks = FIRST_RUN_TICKS;
    char const *env;
    if ((env = getenv("RR_SNAIL_FIRST_RUN_SECONDS")))
        s->runs[0].phase_ticks = atoi(env) * 25;
    if ((env = getenv("RR_SNAIL_PERIOD_SECONDS")))
        run_period_ticks = atoi(env) * 25;
    if ((env = getenv("RR_SNAIL_RARITY")))
        forced_rarity = atoi(env) % rr_rarity_id_max;
    if ((env = getenv("RR_SNAIL_SPEED_PERCENT")))
        speed_scale = atoi(env) / 100.0f;
    if (RR_GLOBAL_BIOME == rr_biome_id_garden)
        build_field(s);
}

void rr_snail_tick(struct rr_server *this)
{
    struct rr_snail *s = &this->snail;
    if (RR_GLOBAL_BIOME != rr_biome_id_garden || !s->field_ready)
        return;
    ++s->ticks;
    // slot 0 is the timed event
    struct rr_snail_run *timed = &s->runs[0];
    switch (timed->phase)
    {
    case rr_snail_phase_idle:
        // the clock only runs while somebody is there to see it
        if (players_in_game(this) > 0 && timed->phase_ticks > 0 &&
            --timed->phase_ticks == 0)
            start_run(this, timed,
                      forced_rarity >= 0 ? forced_rarity : roll_rarity());
        break;
    case rr_snail_phase_running:
        tick_running(this, timed);
        break;
    default:
        if (timed->phase_ticks > 0 && --timed->phase_ticks == 0)
        {
            timed->phase = rr_snail_phase_idle;
            timed->phase_ticks = run_period_ticks;
        }
        break;
    }
    // the others come from the dev panel
    for (uint8_t i = 1; i < RR_SNAIL_MAX_RUNS; ++i)
        if (s->runs[i].phase == rr_snail_phase_running)
            tick_running(this, &s->runs[i]);
}

uint8_t rr_snail_dev_start(struct rr_server *this, uint8_t rarity)
{
    struct rr_snail *s = &this->snail;
    if (RR_GLOBAL_BIOME != rr_biome_id_garden || !s->field_ready ||
        rarity >= rr_rarity_id_max)
        return 0;
    for (uint8_t i = 1; i < RR_SNAIL_MAX_RUNS; ++i)
        if (s->runs[i].phase != rr_snail_phase_running)
        {
            start_run(this, &s->runs[i], rarity);
            return 1;
        }
    return 0;
}

uint8_t rr_snail_is_asleep(struct rr_server *this, EntityIdx mob)
{
    for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS; ++i)
    {
        struct rr_snail_run *r = &this->snail.runs[i];
        if (r->phase == rr_snail_phase_running && r->snail != RR_NULL_ENTITY &&
            r->snail_id == mob)
            return r->waiting;
    }
    return 0;
}

uint8_t rr_snail_mobs_leave_alone(struct rr_server *this, EntityIdx mob)
{
    for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS; ++i)
    {
        struct rr_snail_run *r = &this->snail.runs[i];
        if (r->phase != rr_snail_phase_running || r->snail == RR_NULL_ENTITY ||
            r->snail_id != mob)
            continue;
        if (r->waiting)
            return 1;
        struct rr_component_physical *physical =
            rr_simulation_get_physical(&this->simulation, mob);
        return player_distance_sq(this, physical->x, physical->y) <
               (float)RR_SNAIL_WAKE_RADIUS * RR_SNAIL_WAKE_RADIUS;
    }
    return 0;
}

// ---- the packet: five times a second, the client keeps what it needs

void rr_snail_send_state(struct rr_server *this, struct rr_server_client *client)
{
    struct rr_snail *s = &this->snail;
    if (RR_GLOBAL_BIOME != rr_biome_id_garden || !s->field_ready ||
        s->ticks % 5 != 0)
        return;
    struct proto_bug encoder;
    proto_bug_init(&encoder, outgoing_message);
    proto_bug_write_uint8(&encoder, rr_clientbound_snail_state, "header");
    proto_bug_write_varuint(&encoder, s->event_seq, "snail event seq");
    for (uint8_t e = 0; e < RR_SNAIL_EVENT_HISTORY; ++e)
    {
        proto_bug_write_uint8(&encoder, s->events[e].type, "snail event");
        proto_bug_write_uint8(&encoder, s->events[e].rarity, "snail event r");
        proto_bug_write_uint8(&encoder, s->events[e].count, "snail event n");
    }
    uint8_t count = 0;
    for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS; ++i)
        count += s->runs[i].phase == rr_snail_phase_running;
    proto_bug_write_uint8(&encoder, count, "snail runs");
    if (count > 0)
    {
        proto_bug_write_float32(&encoder, s->hole_x, "hole x");
        proto_bug_write_float32(&encoder, s->hole_y, "hole y");
        for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS; ++i)
        {
            struct rr_snail_run *r = &s->runs[i];
            if (r->phase != rr_snail_phase_running)
                continue;
            proto_bug_write_uint8(&encoder, r->rarity, "snail rarity");
            proto_bug_write_varuint(&encoder, r->route_cells, "snail route");
            proto_bug_write_varuint(&encoder, r->cells_left, "snail left");
            proto_bug_write_uint8(&encoder, r->hp_percent, "snail hp");
            proto_bug_write_uint8(&encoder, r->waiting, "snail waiting");
            proto_bug_write_float32(&encoder, r->snail_x, "snail x");
            proto_bug_write_float32(&encoder, r->snail_y, "snail y");
        }
    }
    rr_server_client_write_message(client, encoder.start,
                                   encoder.current - encoder.start);
}
