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

#include <Server/Simulation.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <Server/EntityAllocation.h>
#include <Server/EntityDetection.h>
#include <Server/SpatialHash.h>
#include <Server/System/System.h>
#include <Server/Waves.h>
#include <Shared/Bitset.h>
#include <Shared/Crypto.h>
#include <Shared/Utilities.h>
#include <Shared/pb.h>

static void set_respawn_zone(struct rr_component_arena *arena, uint32_t x,
                             uint32_t y)
{
    float dim = arena->maze->grid_size;
    arena->respawn_zone.x = 2 * x * dim;
    arena->respawn_zone.y = 2 * y * dim;
}

#define SPAWN_ZONE_X 6
#define SPAWN_ZONE_Y 13

static void set_special_zone(uint8_t biome, uint8_t (*fun)(), uint32_t x,
                             uint32_t y, uint32_t w, uint32_t h)
{
    x *= 2;
    y *= 2;
    w *= 2;
    h *= 2;
    uint32_t dim = RR_MAZES[biome].maze_dim;
    // clamp so a zone that runs off the edge of the template (e.g. one
    // authored close to the border, or scaled up for a bigger map in
    // set_spawn_zones()) doesn't write past the end of maze[]
    if (x >= dim || y >= dim)
        return;
    if (x + w > dim)
        w = dim - x;
    if (y + h > dim)
        h = dim - y;
    for (uint32_t Y = 0; Y < h; ++Y)
        for (uint32_t X = 0; X < w; ++X)
            RR_MAZES[biome].maze[(Y + y) * dim + (X + x)].spawn_function = fun;
}

uint8_t ornith_zone() { return rr_mob_id_ornithomimus; }
uint8_t fern_zone() { return rr_mob_id_fern; }
uint8_t edmon_tree_zone() { return rr_mob_id_edmontosaurus; }
uint8_t quetz_trex_zone()
{
    return rr_frand() > 0.5 ? rr_mob_id_quetzalcoatlus : rr_mob_id_trex;
}
uint8_t trike_dako_zone()
{
    return rr_frand() > 0.2 ? rr_mob_id_dakotaraptor : rr_mob_id_triceratops;
}
uint8_t pter_zone()
{
    return rr_frand() > 0.02 ? rr_mob_id_pteranodon : rr_mob_id_meteor;
}
uint8_t edmo_dako_zone()
{
    return rr_frand() > 0.33 ? rr_mob_id_dakotaraptor : rr_mob_id_edmontosaurus;
}
uint8_t trex_dako_pter_zone()
{
    return rr_frand() > 0.6   ? rr_mob_id_trex
           : rr_frand() > 0.5 ? rr_mob_id_dakotaraptor
                              : rr_mob_id_pteranodon;
}
uint8_t dako_pter_zone()
{
    return rr_frand() > 0.5 ? rr_mob_id_dakotaraptor : rr_mob_id_pteranodon;
}

struct zone
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint8_t (*spawn_func)();
};

#define ZONE_POSITION_COUNT 12
#define ROT_COUNT 7

// all over spawn
static struct zone zone_positions[ZONE_POSITION_COUNT] = {
    {2, 9, 2, 2, ornith_zone},           {8, 22, 2, 2, fern_zone},
    {11, 29, 3, 2, edmon_tree_zone},     {4, 25, 3, 2, quetz_trex_zone},
    {34, 21, 3, 2, trike_dako_zone},     {38, 21, 5, 1, pter_zone},
    {26, 31, 4, 2, edmo_dako_zone},      {17, 19, 3, 2, pter_zone},
    {28, 24, 2, 3, trex_dako_pter_zone}, {0, 11, 5, 1, dako_pter_zone},
    {21, 23, 3, 2, edmon_tree_zone},     {7, 33, 3, 2, trike_dako_zone},
};

// zone_positions is authored against hell_creek_easy's 40x40 template (see
// RR_DEFINE_MAZE(HELL_CREEK_EASY, 80) in StaticData.c). Scale it for
// biomes with a differently-sized template - e.g. hell_creek_med is a 3x3
// tiling of that map (240/80 = 3x the width and height) - so themed zones
// land in the same relative spot across the whole map instead of bunching
// into one corner of it.
#define ZONE_TEMPLATE_DIM 40.0f

static struct zone scale_zone(struct zone z, float scale)
{
    z.x = (uint32_t)(z.x * scale + 0.5f);
    z.y = (uint32_t)(z.y * scale + 0.5f);
    z.w = (uint32_t)(z.w * scale + 0.5f);
    z.h = (uint32_t)(z.h * scale + 0.5f);
    if (z.w == 0)
        z.w = 1;
    if (z.h == 0)
        z.h = 1;
    return z;
}

static void set_spawn_zones()
{
    puts("refreshing spawn zones");
    struct timeval t;
    gettimeofday(&t, NULL);
    int64_t time = (t.tv_sec * 1000000 + t.tv_usec) / (1000 * 1000 * 60 * 30);

    uint64_t seed = rr_get_hash(time);
    float zone_scale =
        (RR_MAZES[RR_GLOBAL_BIOME].maze_dim / 2) / ZONE_TEMPLATE_DIM;

    for (uint64_t i = 0; i < ZONE_POSITION_COUNT; i++)
    {
        struct zone zone = scale_zone(zone_positions[i], zone_scale);
        set_special_zone(RR_GLOBAL_BIOME, NULL, zone.x, zone.y, zone.w,
                         zone.h);
    }

    // shuffle positions
    for (uint64_t i = ZONE_POSITION_COUNT - 1; i > 0; i--)
    {
        uint64_t j = seed % (i + 1);
        struct zone tmp = zone_positions[i];
        zone_positions[i] = zone_positions[j];
        zone_positions[j] = tmp;
        seed = rr_get_hash(seed);
    }

    for (uint64_t i = 0; i < ROT_COUNT; i++)
    {
        struct zone z = scale_zone(zone_positions[i], zone_scale);

        set_special_zone(RR_GLOBAL_BIOME, z.spawn_func, z.x, z.y, z.w,
                         z.h);
    }
}

void rr_simulation_init(struct rr_simulation *this)
{
    memset(this, 0, sizeof *this);
    EntityIdx id = rr_simulation_alloc_entity(this);
    struct rr_component_arena *arena = rr_simulation_add_arena(this, id);
    arena->biome = RR_GLOBAL_BIOME;
    rr_component_arena_spatial_hash_init(arena, this);
    set_respawn_zone(arena, SPAWN_ZONE_X, SPAWN_ZONE_Y);
    set_spawn_zones();
}

struct too_close_captures
{
    struct rr_simulation *simulation;
    float x;
    float y;
    float closest_dist;
    uint8_t done;
};

static void too_close_cb(EntityIdx potential, void *_captures)
{
    struct too_close_captures *captures = _captures;
    if (captures->done)
        return;
    struct rr_simulation *simulation = captures->simulation;
    if (!rr_simulation_has_mob(simulation, potential) &&
            !rr_simulation_has_flower(simulation, potential) ||
        rr_simulation_has_arena(simulation, potential))
        return;
    if (rr_simulation_get_relations(simulation, potential)->team ==
        rr_simulation_team_id_mobs)
        return;
    if (rr_simulation_get_health(simulation, potential)->health == 0)
        return;
    struct rr_component_physical *t_physical =
        rr_simulation_get_physical(simulation, potential);
    struct rr_vector delta = {captures->x - t_physical->x,
                              captures->y - t_physical->y};
    float dist = rr_vector_get_magnitude(&delta);
    if (dist > captures->closest_dist)
        return;
    captures->done = 1;
}

static int too_close(struct rr_simulation *this, float x, float y, float r)
{
    struct too_close_captures shg_captures = {this, x, y, r, 0};
    struct rr_spatial_hash *shg =
        &rr_simulation_get_arena(this, 1)->spatial_hash;
    rr_spatial_hash_query(shg, x, y, r, r, &shg_captures, too_close_cb);
    return shg_captures.done;
}

static void spawn_mob(struct rr_simulation *this, uint32_t grid_x,
                      uint32_t grid_y)
{
    struct rr_component_arena *arena = rr_simulation_get_arena(this, 1);
    struct rr_maze_grid *grid =
        rr_component_arena_get_grid(arena, grid_x, grid_y);
    uint8_t id;

    if (grid->spawn_function != NULL && rr_frand() <
#ifdef RIVET_BUILD
                                            0.75
#else
                                            1
#endif
    )
        id = grid->spawn_function();
    else
        id = get_spawn_id(RR_GLOBAL_BIOME, grid);
    uint8_t rarity =
        get_spawn_rarity(grid->difficulty + grid->local_difficulty * 0.6);
    if (!should_spawn_at(id, rarity))
        return;
    for (uint32_t n = 0; n < 10; ++n)
    {
        struct rr_vector pos = {(grid_x + rr_frand()) * arena->maze->grid_size,
                                (grid_y + rr_frand()) * arena->maze->grid_size};
        if (too_close(this, pos.x, pos.y,
                      RR_MOB_DATA[id].radius *
                              RR_MOB_RARITY_SCALING[rarity].radius +
                          250))
            continue;
        rr_simulation_alloc_mob(this, 1, pos.x, pos.y, id, rarity,
                               rr_simulation_team_id_mobs);
        grid->spawn_timer = 0;
        break;
    }
}

#define PLAYER_COUNT_CAP (12)

// bumped once per tick_maze() call. a grid cell's player_count/
// local_difficulty are only valid for the tick where cell->touch_tick
// equals this counter - see get_active_bounds() below. this lets cells
// be lazily "cleared" the first time something touches them in a new
// tick instead of sweeping every cell in the maze every tick just to
// zero two fields.
static uint32_t current_maze_tick = 0;

// extra radius (world units) simulated beyond a player's FOV, so an
// area's mobs/difficulty are already established by the time the
// player's camera actually reaches it instead of popping in fresh.
#define RR_SPAWN_TICK_MARGIN 2048.0f

// turns a (px +/- radius, py +/- radius) box into an inclusive,
// 2x2-block-aligned grid range clamped to the maze, so callers can walk
// just this range with tick_maze's nw/ne/sw/se block stepping.
static void get_active_bounds(struct rr_component_arena *arena, float px,
                              float py, float radius, uint32_t *sx,
                              uint32_t *sy, uint32_t *ex, uint32_t *ey)
{
    float gs = arena->maze->grid_size;
    int32_t dim = (int32_t)arena->maze->maze_dim;
    int32_t x0 = (int32_t)floorf((px - radius) / gs) & ~1;
    int32_t y0 = (int32_t)floorf((py - radius) / gs) & ~1;
    int32_t x1 = (int32_t)ceilf((px + radius) / gs) | 1;
    int32_t y1 = (int32_t)ceilf((py + radius) / gs) | 1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > dim - 1)
        x1 = dim - 1;
    if (y1 > dim - 1)
        y1 = dim - 1;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    *sx = (uint32_t)x0;
    *sy = (uint32_t)y0;
    *ex = (uint32_t)x1;
    *ey = (uint32_t)y1;
}

static void count_flower_vicinity(EntityIdx entity, void *_simulation)
{
    struct rr_simulation *this = _simulation;
    struct rr_component_arena *arena = rr_simulation_get_arena(this, 1);
    struct rr_component_physical *physical =
        rr_simulation_get_physical(this, entity);
#ifdef RIVET_BUILD
#define FOV 3072
#else
#define FOV 4096
#endif
    uint32_t sx, sy, ex, ey;
    get_active_bounds(arena, physical->x, physical->y,
                      FOV + RR_SPAWN_TICK_MARGIN, &sx, &sy, &ex, &ey);
#undef FOV
    uint32_t level = rr_simulation_get_flower(_simulation, entity)->level;
    for (uint32_t x = sx; x <= ex; ++x)
        for (uint32_t y = sy; y <= ey; ++y)
        {
            struct rr_maze_grid *grid =
                rr_component_arena_get_grid(arena, x, y);
            if (grid->touch_tick != current_maze_tick)
            {
                grid->player_count = 0;
                grid->local_difficulty = 0;
                grid->live_points = 0;
                grid->touch_tick = current_maze_tick;
            }
            grid->player_count += grid->player_count < PLAYER_COUNT_CAP;
            grid->local_difficulty +=
                rr_fclamp((level - (grid->difficulty - 1) * 2.1) / 10, -1, 1);
        }
}

static void despawn_mob(EntityIdx entity, void *_simulation)
{
    struct rr_simulation *this = _simulation;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(this, entity);
    if (physical->arena != 1)
        return;
    if (rr_simulation_has_arena(this, entity))
        return;
    struct rr_component_arena *arena = rr_simulation_get_arena(this, 1);
    struct rr_maze_grid *grid = rr_component_arena_get_grid(
        arena,
        rr_fclamp(physical->x / arena->maze->grid_size, 0,
                  arena->maze->maze_dim - 1),
        rr_fclamp(physical->y / arena->maze->grid_size, 0,
                  arena->maze->maze_dim - 1));
    // a cell nothing touched this tick has nobody near it right now,
    // regardless of whatever player_count it was last left holding
    uint32_t player_count =
        grid->touch_tick == current_maze_tick ? grid->player_count : 0;
    struct rr_component_mob *mob = rr_simulation_get_mob(this, entity);
    if (player_count == 0)
    {
        if (--mob->ticks_to_despawn == 0)
        {
            mob->no_drop = 1;
            rr_simulation_request_entity_deletion(this, entity);
        }
    }
    else
    {
        mob->ticks_to_despawn = 30 * 25;
        // only mobs currently within a player's FOV count against that
        // area's spawn cap - one that wandered off-screen still exists,
        // but shouldn't hold the cap down and block new spawns near the
        // player until it despawns
        grid->live_points += RR_MOB_DIFFICULTY_COEFFICIENTS[mob->id];
    }
}

// mob cap for one grid cell: RR_MOB_CAP_BASE with a single player nearby,
// scaled up by RR_MOB_CAP_PLAYER_STEP per additional player also nearby (so
// 1 player -> 1x cap, 2 -> 1.5x, 3 -> 2x, and so on). player_count only
// counts players whose FOV overlaps this specific cell (see
// count_flower_vicinity), so players on opposite sides of the map never
// raise each other's cap - only ones actually sharing the area do.
#define RR_MOB_CAP_BASE 1.0f
#define RR_MOB_CAP_PLAYER_STEP 0.5f
// once a cell's live mob count reaches this fraction of its cap, spawning
// stops entirely instead of trickling right up to the cap
#define RR_MOB_CAP_SPAWN_GATE 0.75f
// once it's thinned back out below that gate, spawning resumes at this
// multiple of the normal wait (2x wait = half the normal spawn rate)
// rather than bursting straight back up to full rate
#define RR_MOB_CAP_HALF_RATE 2.0f
static float get_max_points(struct rr_maze_grid *grid)
{
    float player_multiplier =
        1.0f + RR_MOB_CAP_PLAYER_STEP * (grid->player_count - 1.0f);
    return RR_MOB_CAP_BASE * player_multiplier * powf(1.1, grid->overload_factor);
}
static int tick_grid(struct rr_simulation *this, struct rr_maze_grid *grid,
                     uint32_t grid_x, uint32_t grid_y)
{
    if (grid->value == 0 || (grid->value & 8))
        return 0;
    grid->local_difficulty =
        rr_fclamp(grid->local_difficulty, -0.5, PLAYER_COUNT_CAP);
    if (grid->local_difficulty > 0)
    {
        grid->overload_factor = rr_fclamp(
            grid->overload_factor + 0.005 * grid->local_difficulty / 25, 0,
            1.5 * grid->local_difficulty);
    }
    else
    {
        grid->overload_factor = rr_fclamp(grid->overload_factor - 0.025 / 25, 0,
                                          grid->overload_factor);
    }
    float player_modifier = 1 + grid->player_count * 4.0 / 3;
    float difficulty_modifier = 150 + 3 * grid->difficulty;
    float overload_modifier =
        powf(1.2, grid->local_difficulty + grid->overload_factor);
    float max_points = get_max_points(grid);
    if (grid->live_points >= max_points * RR_MOB_CAP_SPAWN_GATE)
        return 0;
    float spawn_at = RR_MOB_CAP_HALF_RATE * difficulty_modifier *
                     overload_modifier / (player_modifier);
    if (grid->player_count == 0)
    {
        grid->overload_factor =
            rr_fclamp(grid->overload_factor - 0.025 / 25, 0, 15);
        grid->spawn_timer = rr_frand() * 0.75 * spawn_at;
    }
    else if (grid->spawn_timer >= spawn_at)
    {
        spawn_mob(this, grid_x, grid_y);
        return 1;
    }
    else
        ++grid->spawn_timer;
    return 0;
}

struct tick_blocks_ctx
{
    struct rr_simulation *simulation;
    struct rr_component_arena *arena;
    uint8_t *block_processed;
    uint32_t block_dim;
};

// walks the 2x2 blocks within FOV+margin of one player. block_processed
// dedupes blocks shared by overlapping players so a block is never
// ticked twice in the same tick.
static void tick_blocks_near_flower(EntityIdx entity, void *_ctx)
{
    struct tick_blocks_ctx *ctx = _ctx;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(ctx->simulation, entity);
#ifdef RIVET_BUILD
#define FOV 3072
#else
#define FOV 4096
#endif
    uint32_t sx, sy, ex, ey;
    get_active_bounds(ctx->arena, physical->x, physical->y,
                      FOV + RR_SPAWN_TICK_MARGIN, &sx, &sy, &ex, &ey);
#undef FOV
    for (uint32_t grid_x = sx; grid_x < ex; grid_x += 2)
    {
        for (uint32_t grid_y = sy; grid_y < ey; grid_y += 2)
        {
            uint32_t block_index =
                (grid_y >> 1) * ctx->block_dim + (grid_x >> 1);
            if (rr_bitset_get(ctx->block_processed, block_index))
                continue;
            rr_bitset_set(ctx->block_processed, block_index);

            struct rr_maze_grid *nw =
                rr_component_arena_get_grid(ctx->arena, grid_x, grid_y);
            struct rr_maze_grid *ne =
                rr_component_arena_get_grid(ctx->arena, grid_x + 1, grid_y);
            struct rr_maze_grid *sw =
                rr_component_arena_get_grid(ctx->arena, grid_x, grid_y + 1);
            struct rr_maze_grid *se = rr_component_arena_get_grid(
                ctx->arena, grid_x + 1, grid_y + 1);
            float max_overall = get_max_points(nw) + get_max_points(ne) +
                                get_max_points(sw) + get_max_points(se);
            if (nw->live_points + ne->live_points + sw->live_points +
                    se->live_points >=
                max_overall * RR_MOB_CAP_SPAWN_GATE)
                continue;
            if (tick_grid(ctx->simulation, nw, grid_x, grid_y))
                continue;
            if (tick_grid(ctx->simulation, ne, grid_x + 1, grid_y))
                continue;
            if (tick_grid(ctx->simulation, sw, grid_x, grid_y + 1))
                continue;
            if (tick_grid(ctx->simulation, se, grid_x + 1, grid_y + 1))
                continue;
        }
    }
}

static void tick_maze(struct rr_simulation *this)
{
    struct rr_component_arena *arena = rr_simulation_get_arena(this, 1);
    ++current_maze_tick;

    // only touches cells within FOV+margin of a player - see
    // get_active_bounds(). everything farther away just keeps whatever
    // state it last had, which is fine since nothing below reads it.
    rr_simulation_for_each_flower(this, this, count_flower_vicinity);
    rr_simulation_for_each_mob(this, this, despawn_mob);

    uint32_t block_dim = arena->maze->maze_dim / 2;
    uint8_t block_processed[RR_BITSET_ROUND(block_dim * block_dim)];
    memset(block_processed, 0, sizeof block_processed);
    struct tick_blocks_ctx ctx = {this, arena, block_processed, block_dim};
    rr_simulation_for_each_flower(this, &ctx, tick_blocks_near_flower);
}

#define RR_TIME_BLOCK_(_, CODE)                                                \
    {                                                                          \
        struct timeval start;                                                  \
        struct timeval end;                                                    \
        gettimeofday(&start, NULL);                                            \
        CODE;                                                                  \
        gettimeofday(&end, NULL);                                              \
        uint64_t elapsed_time = (end.tv_sec - start.tv_sec) * 1000000 +        \
                                (end.tv_usec - start.tv_usec);                 \
        if (elapsed_time > 3000)                                               \
        {                                                                      \
            printf(_ " took %lu microseconds with %d entities\n",              \
                   elapsed_time, this->physical_count);                        \
        }                                                                      \
    };

#define RR_TIME_BLOCK(_, CODE)                                                 \
    {                                                                          \
        CODE;                                                                  \
    };

static int64_t last_zone_epoch = -1;

void rr_simulation_tick(struct rr_simulation *this)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    int64_t time = (t.tv_sec * 1000000 + t.tv_usec);
    int64_t current_zone_epoch = time / (1000 * 1000 * 60 * 30);

    if (current_zone_epoch != last_zone_epoch)
    {
        set_spawn_zones();
        last_zone_epoch = current_zone_epoch;
    }
    rr_simulation_create_component_vectors(this);
    RR_TIME_BLOCK("collision_detection",
                  { rr_system_collision_detection_tick(this); });
    RR_TIME_BLOCK("ai", { rr_system_ai_tick(this); });
    RR_TIME_BLOCK("drops", { rr_system_drops_tick(this); });
    RR_TIME_BLOCK("portal", { rr_system_portal_tick(this); });
    RR_TIME_BLOCK("checkpoints", { rr_system_checkpoints_tick(this); });
    RR_TIME_BLOCK("petal_behavior", { rr_system_petal_behavior_tick(this); });
    RR_TIME_BLOCK("collision_resolution",
                  { rr_system_collision_resolution_tick(this); });
    RR_TIME_BLOCK("web", { rr_system_web_tick(this); });
    RR_TIME_BLOCK("velocity", { rr_system_velocity_tick(this); });
    RR_TIME_BLOCK("centipede", { rr_system_centipede_tick(this); });
    RR_TIME_BLOCK("health", { rr_system_health_tick(this); });
    RR_TIME_BLOCK("camera", { rr_system_camera_tick(this); });
    RR_TIME_BLOCK("spawn_tick", { tick_maze(this); });
    memcpy(this->deleted_last_tick, this->pending_deletions,
           sizeof this->pending_deletions);
    memset(this->pending_deletions, 0, sizeof this->pending_deletions);
    RR_TIME_BLOCK("free_component", {
        rr_bitset_for_each_bit(
            this->deleted_last_tick,
            this->deleted_last_tick + (RR_BITSET_ROUND(RR_MAX_ENTITY_COUNT)),
            this, __rr_simulation_pending_deletion_free_components);
    });
    RR_TIME_BLOCK("unset_entity", {
        rr_bitset_for_each_bit(
            this->deleted_last_tick,
            this->deleted_last_tick + RR_BITSET_ROUND(RR_MAX_ENTITY_COUNT),
            this, __rr_simulation_pending_deletion_unset_entity);
    });
}

int rr_simulation_entity_alive(struct rr_simulation *this, EntityHash hash)
{
    return this->entity_tracker[(EntityIdx)hash] &&
           this->entity_hash_tracker[(EntityIdx)hash] == (hash >> 16) &&
           !rr_bitset_get(this->deleted_last_tick, (EntityIdx)hash);
}

EntityHash rr_simulation_get_entity_hash(struct rr_simulation *this,
                                         EntityIdx id)
{
    return ((uint32_t)(this->entity_hash_tracker[id]) << 16) | id;
}
