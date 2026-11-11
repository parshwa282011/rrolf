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

// A test of the garden's mobs on a simulation, with no network and no clients:
// ant holes are built with the right population, eggs hatch and the queen lays
// new ones, killing the queen lets the others out, plants leave holes behind at the rate they should. Built with -DGARDEN by
// tools/garden/run-sim-test.sh.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Server/Client.h>
#include <Server/EntityAllocation.h>
#include <Server/EntityDetection.h>
#include <Server/Server.h>
#include <Server/Simulation.h>
#include <Server/Snail.h>
#include <Server/Waves.h>
#include <Shared/Component/Arena.h>
#include <Shared/Component/Drop.h>
#include <Shared/Binary.h>
#include <Shared/Component/Flower.h>
#include <Shared/Component/Health.h>
#include <Shared/Component/Mob.h>
#include <Shared/StaticData.h>
#include <Shared/Tags.h>
#include <Shared/pb.h>

static struct rr_server server;
static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        ++checks;                                                              \
        if (cond)                                                              \
            printf("  ok    %s\n", #cond);                                     \
        else                                                                   \
        {                                                                      \
            ++failures;                                                        \
            printf("  FAIL  %s   ", #cond);                                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static struct rr_simulation *sim() { return &server.simulation; }

// what the master sends a game server when a player joins (MasterServer/c/games.c send_join_ok,
// after the byte that says it is a join), to try how the server reads it. `mob` is an id of the mob
// gallery (id + 1 as in the message), `tag` is NULL for the master from before the tags
static size_t fake_join(uint8_t *out, uint64_t nonce, uint8_t gallery_id, char const *tag)
{
    struct rr_binary_encoder e;
    rr_binary_encoder_init(&e, out);
    rr_binary_encoder_write_varuint(&e, nonce);
    rr_binary_encoder_write_nt_string(&e, "uuid-1");
    rr_binary_encoder_write_nt_string(&e, "token");
    rr_binary_encoder_write_nt_string(&e, "Name");
    rr_binary_encoder_write_nt_string(&e, "");
    rr_binary_encoder_write_nt_string(&e, "");
    rr_binary_encoder_write_uint8(&e, 0); // not a dev
    rr_binary_encoder_write_float64(&e, 1234);
    rr_binary_encoder_write_varuint(&e, 0);
    for (uint8_t b = 0; b < RR_CLIENT_SAVED_BIOMES; ++b)
        rr_binary_encoder_write_uint8(&e, 0);
    rr_binary_encoder_write_uint8(&e, rr_petal_id_basic); // one petal
    rr_binary_encoder_write_uint8(&e, 0);
    rr_binary_encoder_write_varuint(&e, 5);
    rr_binary_encoder_write_uint8(&e, 0);
    rr_binary_encoder_write_uint8(&e, 0); // no failed crafts
    rr_binary_encoder_write_uint8(&e, gallery_id); // one mob kill count
    rr_binary_encoder_write_uint8(&e, rr_rarity_id_rare);
    rr_binary_encoder_write_varuint(&e, 7);
    rr_binary_encoder_write_uint8(&e, 0);
    rr_binary_encoder_write_uint8(&e, 0); // no loadout
    rr_binary_encoder_write_nt_string(&e, "Nick");
    if (tag != NULL)
        rr_binary_encoder_write_nt_string(&e, (char *)tag);
    return (size_t)(e.at - e.start);
}

static uint8_t flower_state_high_bit(struct rr_component_flower *flower, uint8_t is_creation)
{
    static uint8_t buffer[256];
    struct proto_bug pb;
    proto_bug_init(&pb, buffer);
    rr_component_flower_write(flower, &pb, is_creation, NULL);
    proto_bug_init(&pb, buffer);
    return (proto_bug_read_varuint(&pb, "state") & 0x100) != 0;
}

// A hive or hole nobody is near unloads after 30 seconds, and there are no
// players here, so the holes are told that somebody is
static uint8_t any_target(struct rr_simulation *s, EntityIdx seeker,
                          EntityIdx target, void *captures)
{
    (void)s; (void)seeker; (void)target; (void)captures;
    return 1;
}

static void count_drop(EntityIdx entity, void *count)
{
    (void)entity;
    ++*(uint32_t *)count;
}

struct drop_check
{
    uint32_t seen, wrong_owner, only_one_owner, far;
    float x, y;
};

static void check_drop(EntityIdx entity, void *_check)
{
    struct drop_check *check = _check;
    struct rr_component_drop *drop = rr_simulation_get_drop(sim(), entity);
    // only what the snail gives (earlier tests may have left other drops)
    if (drop->id != rr_petal_id_shell && drop->id != rr_petal_id_mint && drop->id != rr_petal_id_nazar)
        return;
    ++check->seen;
    uint32_t owners = 0;
    for (uint32_t i = 0; i < RR_MAX_CLIENT_COUNT; ++i)
        owners += rr_bitset_get_bit(drop->can_be_picked_up_by, i) != 0;
    check->only_one_owner += owners == 1;
    if (!(rr_bitset_get_bit(drop->can_be_picked_up_by, 0) || rr_bitset_get_bit(drop->can_be_picked_up_by, 1)))
        ++check->wrong_owner;
    struct rr_component_physical *p = rr_simulation_get_physical(sim(), entity);
    if (fabsf(p->x - check->x) > 400 || fabsf(p->y - check->y) > 400)
        ++check->far;
}

static void tick(uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
    {
        rr_simulation_create_component_vectors(sim());
        for (uint32_t j = 0; j < sim()->arena_count; ++j)
            if (rr_simulation_has_mob(sim(), sim()->arena_vector[j]))
                rr_simulation_get_mob(sim(), sim()->arena_vector[j])
                    ->unvisited_ticks = 30 * 25;
        rr_simulation_tick(sim());
    }
}

// how many mobs of an id are in an arena
static uint32_t count_mobs(EntityIdx arena, uint8_t id)
{
    rr_simulation_create_component_vectors(sim());
    uint32_t count = 0;
    for (uint32_t i = 0; i < sim()->mob_count; ++i)
    {
        EntityIdx e = sim()->mob_vector[i];
        if (rr_simulation_get_physical(sim(), e)->arena == arena &&
            rr_simulation_get_mob(sim(), e)->id == id)
            ++count;
    }
    return count;
}

static EntityIdx find_mob(EntityIdx arena, uint8_t id)
{
    rr_simulation_create_component_vectors(sim());
    for (uint32_t i = 0; i < sim()->mob_count; ++i)
    {
        EntityIdx e = sim()->mob_vector[i];
        if (rr_simulation_get_physical(sim(), e)->arena == arena &&
            rr_simulation_get_mob(sim(), e)->id == id)
            return e;
    }
    return RR_NULL_ENTITY;
}

static EntityIdx make_hole(uint8_t rarity)
{
    return rr_simulation_alloc_mob(sim(), 1, 40000, 40000, rr_mob_id_ant_hole,
                                   rarity, rr_simulation_team_id_mobs);
}

// deletes a hole and what is in it, the way its collapse does
static void remove_hole(EntityIdx hole)
{
    rr_simulation_create_component_vectors(sim());
    for (uint32_t i = 0; i < sim()->mob_count; ++i)
    {
        EntityIdx e = sim()->mob_vector[i];
        if (rr_simulation_get_physical(sim(), e)->arena == hole)
        {
            rr_simulation_get_mob(sim(), e)->no_drop = 1;
            rr_simulation_request_entity_deletion(sim(), e);
        }
    }
    rr_simulation_get_mob(sim(), hole)->no_drop = 1;
    rr_simulation_request_entity_deletion(sim(), hole);
    tick(2);
}

static void kill(EntityIdx e)
{
    rr_component_health_set_health(rr_simulation_get_health(sim(), e), 0);
}

int main()
{
    srand(12345);
    rr_server_init(&server);
    CHECK(RR_GLOBAL_BIOME == rr_biome_id_garden, "built with -DGARDEN");

    printf("every hole is built with the population of its rarity\n");
    for (uint8_t rarity = 0; rarity < rr_rarity_id_max; ++rarity)
    {
        struct rr_ant_hole_rarity_scale const *scale =
            &RR_ANT_HOLE_RARITY_SCALING[rarity];
        EntityIdx hole = make_hole(rarity);
        struct rr_component_arena *arena =
            rr_simulation_get_arena(sim(), hole);
        CHECK(arena->biome == rr_biome_id_ant_hole, "the hole has its own biome");
        CHECK(count_mobs(hole, rr_mob_id_queen_ant) == 1, "one queen at rarity %u", rarity);
        CHECK(count_mobs(hole, rr_mob_id_ant_egg) == scale->egg_count,
              "%u eggs at rarity %u, got %u", scale->egg_count, rarity,
              count_mobs(hole, rr_mob_id_ant_egg));
        CHECK(arena->ant_egg_count == scale->egg_count, "the egg count is kept (%u)", arena->ant_egg_count);
        CHECK(count_mobs(hole, rr_mob_id_ant) == scale->worker_count, "%u workers at rarity %u", scale->worker_count, rarity);
        CHECK(count_mobs(hole, rr_mob_id_baby_ant) == scale->baby_ant_count, "%u babies at rarity %u", scale->baby_ant_count, rarity);
        uint32_t all = 1 + scale->egg_count + scale->worker_count + scale->baby_ant_count;
        CHECK(arena->mob_count == all, "mob_count %u is what is in it, %u", arena->mob_count, all);
        // a hole is the size of a common one at every rarity (the other mobs grow)
        float radius = rr_simulation_get_physical(sim(), hole)->radius;
        CHECK(radius == RR_MOB_DATA[rr_mob_id_ant_hole].radius, "the hole of rarity %u is %.0f wide, a common one is %.0f",
              rarity, radius, RR_MOB_DATA[rr_mob_id_ant_hole].radius);
        remove_hole(hole);
    }

    printf("the other mobs still grow with their rarity\n");
    for (uint8_t rarity = 1; rarity < rr_rarity_id_max; ++rarity)
    {
        EntityIdx ant = rr_simulation_alloc_mob(sim(), 1, 9500, 9500, rr_mob_id_ant, rarity, rr_simulation_team_id_mobs);
        CHECK(rr_simulation_get_physical(sim(), ant)->radius > RR_MOB_DATA[rr_mob_id_ant].radius, "an ant of rarity %u is bigger than a common one", rarity);
        rr_simulation_request_entity_deletion(sim(), ant);
    }
    tick(2);

    printf("eggs hatch and the queen lays new ones\n");
    {
        EntityIdx hole = make_hole(rr_rarity_id_rare);
        uint32_t cap = RR_ANT_HOLE_RARITY_SCALING[rr_rarity_id_rare].egg_count;
        uint32_t babies = count_mobs(hole, rr_mob_id_baby_ant);
        tick(11 * 25); // an egg takes 6 to 9 seconds
        uint32_t eggs_now = count_mobs(hole, rr_mob_id_ant_egg);
        CHECK(count_mobs(hole, rr_mob_id_baby_ant) > babies, "babies hatched (%u -> %u)", babies, count_mobs(hole, rr_mob_id_baby_ant));
        CHECK(eggs_now <= cap, "never more eggs than the cap (%u of %u)", eggs_now, cap);
        CHECK(rr_simulation_get_arena(sim(), hole)->ant_egg_count == eggs_now, "the egg counter matches the eggs");
        tick(30 * 25);
        CHECK(count_mobs(hole, rr_mob_id_ant_egg) <= cap, "still not more than the cap");
        CHECK(count_mobs(hole, rr_mob_id_ant_egg) >= 1, "the queen keeps laying (%u eggs)", count_mobs(hole, rr_mob_id_ant_egg));
        CHECK(rr_simulation_get_arena(sim(), hole)->ant_egg_count == count_mobs(hole, rr_mob_id_ant_egg), "the counter still matches");

        printf("killing the queen lets the others out and removes the hole\n");
        uint32_t workers = count_mobs(hole, rr_mob_id_ant);
        uint32_t kids = count_mobs(hole, rr_mob_id_baby_ant);
        uint32_t outside_before = count_mobs(1, rr_mob_id_ant) + count_mobs(1, rr_mob_id_baby_ant);
        kill(find_mob(hole, rr_mob_id_queen_ant));
        tick(3);
        CHECK(!rr_simulation_has_entity(sim(), hole), "the hole is gone");
        CHECK(count_mobs(hole, rr_mob_id_ant_egg) == 0, "no eggs are left");
        uint32_t outside_after = count_mobs(1, rr_mob_id_ant) + count_mobs(1, rr_mob_id_baby_ant);
        CHECK(outside_after - outside_before == workers + kids,
              "%u ants came out, expected %u", outside_after - outside_before, workers + kids);
        CHECK(rr_simulation_has_entity(sim(), 1), "the main arena is still there");
    }

    printf("a hole nobody goes near unloads, one somebody went in does not\n");
    {
        EntityIdx idle = make_hole(rr_rarity_id_common);
        EntityIdx visited = make_hole(rr_rarity_id_common);
        rr_simulation_get_arena(sim(), visited)->player_entered = 1;
        for (uint32_t t = 0; t < 31 * 25; ++t)
            rr_simulation_tick(sim());
        CHECK(!rr_simulation_has_entity(sim(), idle) ||
                  !rr_simulation_has_mob(sim(), idle) ||
                  rr_simulation_get_mob(sim(), idle)->id != rr_mob_id_ant_hole,
              "the hole nobody was near is gone");
        CHECK(rr_simulation_has_entity(sim(), visited) &&
                  rr_simulation_get_mob(sim(), visited)->id == rr_mob_id_ant_hole,
              "the hole that was entered stays, it has its own timer");
        remove_hole(visited);
    }

    printf("plants leave holes and hives at the right rates\n");
    {
        uint32_t holes_before = count_mobs(1, rr_mob_id_ant_hole);
        enum { PLANTS = 1000 };
        for (uint8_t kind = 0; kind < 3; ++kind)
        {
            uint8_t id = kind == 0 ? rr_mob_id_bush : kind == 1 ? rr_mob_id_fern : rr_mob_id_tree;
            uint8_t result = kind == 2 ? rr_mob_id_beehive : rr_mob_id_ant_hole;
            uint32_t before = count_mobs(1, result);
            for (uint32_t i = 0; i < PLANTS; ++i)
            {
                EntityIdx plant = rr_simulation_alloc_mob(
                    sim(), 1, 4000 + 40 * (i % 100), 4000 + 40 * (i / 100), id,
                    rr_rarity_id_common, rr_simulation_team_id_mobs);
                kill(plant);
                if (i % 100 == 99)
                    tick(2);
            }
            tick(2);
            uint32_t made = count_mobs(1, result) - before;
            float want = PLANTS * (kind == 0 ? RR_ANT_HOLE_FROM_BUSH_CHANCE
                                   : kind == 1 ? RR_ANT_HOLE_FROM_FERN_CHANCE
                                               : RR_BEEHIVE_FROM_TREE_CHANCE);
            float spread = 4 * sqrtf(want);
            printf("  %u %s deaths -> %u (expected about %.0f)\n", PLANTS,
                   RR_MOB_NAMES[id], made, want);
            CHECK(fabsf(made - want) < spread, "%s rate is right", RR_MOB_NAMES[id]);
            // clear them so that the entities can be used again
            rr_simulation_create_component_vectors(sim());
            for (uint32_t i = 0; i < sim()->mob_count; ++i)
            {
                EntityIdx e = sim()->mob_vector[i];
                if (rr_simulation_get_mob(sim(), e)->id == result)
                {
                    rr_simulation_get_mob(sim(), e)->no_drop = 1;
                    rr_simulation_request_entity_deletion(sim(), e);
                }
            }
            tick(2);
        }
        CHECK(count_mobs(1, rr_mob_id_ant_hole) == holes_before, "all holes were cleared");
    }

    printf("a snail starts asleep in the zone of its rarity, hunted by mobs as rare as itself\n");
    {
        for (uint8_t trial = 0; trial < 80; ++trial)
        {
            // a rarity below epic is raised to epic
            uint8_t asked = trial % rr_rarity_id_max;
            rr_snail_dev_start(&server, asked);
            struct rr_snail_run *run = &server.snail.runs[1];
            CHECK(run->phase == rr_snail_phase_running, "a run started");
            EntityIdx snail = run->snail_id;
            struct rr_component_physical *p = rr_simulation_get_physical(sim(), snail);
            uint32_t cx = p->x / (2 * RR_MAZES[rr_biome_id_garden].grid_size);
            uint32_t cy = p->y / (2 * RR_MAZES[rr_biome_id_garden].grid_size);
            uint8_t difficulty = RR_MAZE_TEMPLATE_GARDEN[cy][cx];
            uint8_t min = RR_SNAIL_ZONE_MIN(RR_SNAIL_START_RARITY(run->rarity)),
                    max = RR_SNAIL_ZONE_MAX(RR_SNAIL_START_RARITY(run->rarity));
            CHECK(asked < RR_SNAIL_MIN_RARITY ? run->rarity == RR_SNAIL_MIN_RARITY : run->rarity == asked,
                  "asked for %u, got %u", asked, run->rarity);
            CHECK(difficulty >= min && difficulty <= max,
                  "a rarity %u snail starts at difficulty %u (%u,%u), zone %u to %u", run->rarity, difficulty, cx, cy, min, max);
            if (trial == 0)
            {
                CHECK(run->waiting, "asleep");
                CHECK(rr_simulation_get_health(sim(), snail)->invulnerable, "nothing hurts it");
                float x = p->x, y = p->y;
                struct rr_component_health *h = rr_simulation_get_health(sim(), snail);
                float hp = h->health;
                EntityIdx hitter = rr_simulation_alloc_mob(sim(), 1, x + 300, y, rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
                for (uint32_t t = 0; t < 120; ++t)
                {
                    rr_component_health_do_damage(sim(), h, hitter, 5, 0);
                    rr_simulation_tick(sim());
                    rr_snail_tick(&server);
                }
                CHECK(run->waiting && run->phase == rr_snail_phase_running, "still asleep with nobody around");
                CHECK(rr_simulation_find_nearest_enemy(sim(), hitter, 2000, NULL, any_target) != snail,
                      "mobs do not go for a snail that has not set off");
                CHECK(rr_snail_mobs_leave_alone(&server, snail) == 1, "the sleeping snail is left alone");
                CHECK(fabsf(p->x - x) < 1 && fabsf(p->y - y) < 1, "it did not move");
                CHECK(h->health == hp, "it took no damage");
                struct rr_component_arena *arena = rr_simulation_get_arena(sim(), 1);
                struct rr_maze_grid *grid = rr_component_arena_get_grid(arena, p->x / arena->maze->grid_size, p->y / arena->maze->grid_size);
                CHECK(grid->player_count == 0 && grid->snail_difficulty == 0,
                      "asleep it loads nothing (%u, %.0f)", grid->player_count, grid->snail_difficulty);
                // once it has set off it does
                run->waiting = 0;
                h->invulnerable = 0;
                tick(2);
                CHECK(rr_snail_mobs_leave_alone(&server, snail) == 0, "with no player near it is not left alone");
                CHECK(rr_simulation_find_nearest_enemy(sim(), hitter, 2000, NULL, any_target) == snail,
                      "and mobs go for it");
                CHECK(grid->player_count > 0, "once it set off the cell it is in counts as loaded");
                CHECK(grid->snail_difficulty == RR_SNAIL_HUNT_DIFFICULTY(run->rarity),
                      "mobs there spawn as for difficulty %.0f (%.0f)", (float)RR_SNAIL_HUNT_DIFFICULTY(run->rarity), grid->snail_difficulty);
            }
            // end the run again
            rr_simulation_get_mob(sim(), snail)->no_drop = 1;
            rr_simulation_request_entity_deletion(sim(), snail);
            run->phase = rr_snail_phase_idle;
            run->snail = RR_NULL_ENTITY;
            tick(2);
        }
    }

    printf("a sleeping snail keeps the mobs 500 units away\n");
    {
        rr_snail_dev_start(&server, rr_rarity_id_epic);
        struct rr_snail_run *run = &server.snail.runs[1];
        struct rr_component_physical *sp = rr_simulation_get_physical(sim(), run->snail_id);
        EntityIdx near[3];
        float offsets[3][2] = {{100, 0}, {0, -250}, {-5, 5}};
        for (uint8_t i = 0; i < 3; ++i)
            near[i] = rr_simulation_alloc_mob(sim(), 1, sp->x + offsets[i][0], sp->y + offsets[i][1], rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
        for (uint32_t t = 0; t < 5; ++t)
        {
            tick(1);
            rr_snail_tick(&server);
        }
        for (uint8_t i = 0; i < 3; ++i)
        {
            struct rr_component_physical *mp = rr_simulation_get_physical(sim(), near[i]);
            float d = sqrtf((mp->x - sp->x) * (mp->x - sp->x) + (mp->y - sp->y) * (mp->y - sp->y));
            CHECK(d >= RR_SNAIL_KEEP_OUT_RADIUS - 2, "mob %u is %.0f units from the sleeping snail", i, d);
        }
        // it wakes for a player right next to it, not for one a little farther
        {
            EntityIdx flower = rr_simulation_alloc_entity(sim());
            struct rr_component_physical *fp = rr_simulation_add_physical(sim(), flower);
            rr_simulation_add_flower(sim(), flower);
            fp->arena = 1;
            static struct rr_component_player_info player_info;
            memset(&player_info, 0, sizeof player_info);
            player_info.flower_id = flower;
            player_info.client = &server.clients[0];
            server.clients[0].player_info = &player_info;
            server.clients[0].disconnected = 0;
            rr_bitset_set(server.clients_in_use, 0);
            float away[3] = {2400, 1000, RR_SNAIL_START_RADIUS + 50};
            for (uint8_t i = 0; i < 3; ++i)
            {
                rr_component_physical_set_x(fp, sp->x + away[i]);
                rr_component_physical_set_y(fp, sp->y);
                rr_snail_tick(&server);
                CHECK(run->waiting, "it sleeps on with a player %.0f units away", away[i]);
            }
            rr_component_physical_set_x(fp, sp->x + RR_SNAIL_START_RADIUS - 50);
            rr_snail_tick(&server);
            CHECK(!run->waiting, "and wakes for one %d units away", RR_SNAIL_START_RADIUS - 50);
            rr_bitset_unset(server.clients_in_use, 0);
            server.clients[0].player_info = NULL;
            rr_simulation_request_entity_deletion(sim(), flower);
            run->waiting = 1;
            rr_simulation_get_health(sim(), run->snail_id)->invulnerable = 1;
        }
        // a woken snail does not push anybody
        run->waiting = 0;
        rr_simulation_get_health(sim(), run->snail_id)->invulnerable = 0;
        EntityIdx close = rr_simulation_alloc_mob(sim(), 1, sp->x + 60, sp->y, rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
        rr_snail_tick(&server);
        struct rr_component_physical *cp = rr_simulation_get_physical(sim(), close);
        CHECK(fabsf(cp->x - (sp->x + 60)) < 30, "once it is on its way a mob can be next to it");
        for (uint8_t i = 0; i < 3; ++i)
            rr_simulation_request_entity_deletion(sim(), near[i]);
        rr_simulation_request_entity_deletion(sim(), close);
        rr_simulation_get_mob(sim(), run->snail_id)->no_drop = 1;
        rr_simulation_request_entity_deletion(sim(), run->snail_id);
        run->phase = rr_snail_phase_idle;
        run->snail = RR_NULL_ENTITY;
        tick(2);
    }

    printf("the snail's way home is the cheapest one: it goes round the hard parts\n");
    {
        struct rr_snail *sn = &server.snail;
        uint32_t cells = RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM;
        // the shortest way (fewest cells) from every cell, by breadth first search,
        // and what it costs
        static uint16_t dist[RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM];
        static uint32_t short_cost[RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM];
        static uint16_t queue[RR_SNAIL_TEMPLATE_DIM * RR_SNAIL_TEMPLATE_DIM];
        for (uint32_t i = 0; i < cells; ++i)
            dist[i] = 0xffff;
        uint32_t hole = 21 * RR_SNAIL_TEMPLATE_DIM + 20, head = 0, tail = 0;
        dist[hole] = 0;
        short_cost[hole] = 0;
        queue[tail++] = hole;
        while (head < tail)
        {
            uint16_t at = queue[head++];
            int32_t x = at % RR_SNAIL_TEMPLATE_DIM, y = at / RR_SNAIL_TEMPLATE_DIM;
            int8_t const step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (uint8_t i = 0; i < 4; ++i)
            {
                int32_t nx = x + step[i][0], ny = y + step[i][1];
                if (nx < 0 || ny < 0 || nx >= RR_SNAIL_TEMPLATE_DIM || ny >= RR_SNAIL_TEMPLATE_DIM ||
                    RR_MAZE_TEMPLATE_GARDEN[ny][nx] == 0)
                    continue;
                uint16_t n = ny * RR_SNAIL_TEMPLATE_DIM + nx;
                if (dist[n] != 0xffff)
                    continue;
                dist[n] = dist[at] + 1;
                short_cost[n] = short_cost[at] + RR_SNAIL_CELL_COST(RR_MAZE_TEMPLATE_GARDEN[at / RR_SNAIL_TEMPLATE_DIM][at % RR_SNAIL_TEMPLATE_DIM]);
                queue[tail++] = n;
            }
        }
        uint32_t never_dearer = 1, longer = 0, cheaper = 0, reachable = 0;
        for (uint32_t i = 0; i < cells; ++i)
        {
            if (dist[i] == 0xffff)
                continue;
            ++reachable;
            if (sn->cost[i] > short_cost[i])
                never_dearer = 0;
            longer += sn->steps[i] > dist[i];
            cheaper += sn->cost[i] < short_cost[i];
        }
        CHECK(reachable > 0 && never_dearer, "no way is dearer than the shortest one");
        CHECK(longer > 0 && cheaper > 0, "and %u cells have a longer way that is cheaper", longer);
        // starting where a snail does, the way is never dearer either
        uint32_t checked = 0;
        for (uint8_t rarity = RR_SNAIL_MIN_RARITY; rarity < rr_rarity_id_max; ++rarity)
        {
            rr_snail_dev_start(&server, rarity);
            struct rr_snail_run *run = &server.snail.runs[1];
            struct rr_component_physical *p = rr_simulation_get_physical(sim(), run->snail_id);
            uint32_t cx = p->x / (2 * RR_MAZES[rr_biome_id_garden].grid_size);
            uint32_t cy = p->y / (2 * RR_MAZES[rr_biome_id_garden].grid_size);
            checked += sn->cost[cy * RR_SNAIL_TEMPLATE_DIM + cx] <= short_cost[cy * RR_SNAIL_TEMPLATE_DIM + cx];
            printf("  rarity %u starts at (%u,%u): %u cells, cost %u (the shortest is %u cells, cost %u)\n", rarity, cx, cy,
                   sn->steps[cy * RR_SNAIL_TEMPLATE_DIM + cx], sn->cost[cy * RR_SNAIL_TEMPLATE_DIM + cx], dist[cy * RR_SNAIL_TEMPLATE_DIM + cx], short_cost[cy * RR_SNAIL_TEMPLATE_DIM + cx]);
            rr_simulation_get_mob(sim(), run->snail_id)->no_drop = 1;
            rr_simulation_request_entity_deletion(sim(), run->snail_id);
            run->phase = rr_snail_phase_idle;
            run->snail = RR_NULL_ENTITY;
            tick(2);
        }
        CHECK(checked == rr_rarity_id_max - RR_SNAIL_MIN_RARITY, "from every start");
    }

    printf("the rewards are drops on the ground, one set for each player\n");
    {
        EntityIdx marker = rr_simulation_alloc_mob(sim(), 1, 9000, 9000, rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
        struct rr_component_physical *at = rr_simulation_get_physical(sim(), marker);
        float px = at->x, py = at->y;
        uint32_t drops_before = 0;
        rr_simulation_for_each_drop(sim(), &drops_before, count_drop);
        struct drop_check old_ones = {0, 0, 0, 0, px, py};
        rr_simulation_for_each_drop(sim(), &old_ones, check_drop);
        CHECK(old_ones.seen == 0, "no shells, mints or nazars lying around before (%u)", old_ones.seen);
        uint32_t got_a = 0, got_b = 0, total_c = 0;
        for (uint8_t trial = 0; trial < 50; ++trial)
        {
            got_a += rr_snail_give_rewards(&server, &server.clients[0], at, rr_rarity_id_epic);
            got_b += rr_snail_give_rewards(&server, &server.clients[1], at, rr_rarity_id_ancient);
            ++total_c;
        }
        tick(1);
        uint32_t drops_after = 0;
        rr_simulation_for_each_drop(sim(), &drops_after, count_drop);
        CHECK(got_a >= 2 * total_c && got_b >= 2 * total_c, "every player gets at least a shell and a mint (%u, %u)", got_a, got_b);
        CHECK(drops_after - drops_before == got_a + got_b, "each one is a drop entity (%u new)", drops_after - drops_before);
        struct drop_check check = {0, 0, 0, 0, px, py};
        rr_simulation_for_each_drop(sim(), &check, check_drop);
        CHECK(check.wrong_owner == 0 && check.only_one_owner == check.seen, "each is for one player only");
        CHECK(check.far == 0, "and they are where the player is");
    }

    printf("the dev panel can have several snails going at once\n");
    {
        uint8_t started = 0;
        for (uint8_t i = 0; i < RR_SNAIL_MAX_RUNS + 3; ++i)
            started += rr_snail_dev_start(&server, rr_rarity_id_epic + i % 5);
        CHECK(started == RR_SNAIL_MAX_RUNS - 1, "%u started, slot 0 is the timed event's", started);
        uint8_t running = 0, asleep = 0;
        for (uint8_t i = 1; i < RR_SNAIL_MAX_RUNS; ++i)
        {
            struct rr_snail_run *r = &server.snail.runs[i];
            running += r->phase == rr_snail_phase_running;
            asleep += r->waiting;
            CHECK(rr_simulation_has_entity(sim(), r->snail_id) && rr_simulation_get_mob(sim(), r->snail_id)->id == rr_mob_id_snail,
                  "slot %u has a snail", i);
        }
        CHECK(running == RR_SNAIL_MAX_RUNS - 1 && asleep == running, "all running and asleep");
        CHECK(server.snail.runs[0].phase == rr_snail_phase_idle, "the timed event was not touched");
        tick(5);
        rr_snail_tick(&server);
        // one asleep snail is known as asleep, an ant is not
        CHECK(rr_snail_is_asleep(&server, server.snail.runs[3].snail_id) == 1, "asleep is known");
        CHECK(rr_snail_is_asleep(&server, find_mob(1, rr_mob_id_ant)) == 0 || find_mob(1, rr_mob_id_ant) == RR_NULL_ENTITY, "an ant is not a snail");
        // wake one of them: only that one loads the map
        server.snail.runs[2].waiting = 0;
        rr_simulation_get_health(sim(), server.snail.runs[2].snail_id)->invulnerable = 0;
        CHECK(rr_snail_is_asleep(&server, server.snail.runs[2].snail_id) == 0 && rr_snail_is_asleep(&server, server.snail.runs[4].snail_id) == 1,
              "waking one leaves the others asleep");
        for (uint8_t i = 1; i < RR_SNAIL_MAX_RUNS; ++i)
        {
            struct rr_snail_run *r = &server.snail.runs[i];
            rr_simulation_get_mob(sim(), r->snail_id)->no_drop = 1;
            rr_simulation_request_entity_deletion(sim(), r->snail_id);
            r->phase = rr_snail_phase_idle;
            r->snail = RR_NULL_ENTITY;
        }
        tick(2);
        CHECK(rr_snail_dev_start(&server, rr_rarity_id_epic) == 1, "and the slots are free again");
        struct rr_snail_run *r = &server.snail.runs[1];
        rr_simulation_get_mob(sim(), r->snail_id)->no_drop = 1;
        rr_simulation_request_entity_deletion(sim(), r->snail_id);
        r->phase = rr_snail_phase_idle;
        r->snail = RR_NULL_ENTITY;
        tick(2);
    }

    printf("a nazar saves you from hits that would have killed you\n");
    {
        EntityIdx victim = rr_simulation_alloc_mob(sim(), 1, 9000, 9000, rr_mob_id_ant, rr_rarity_id_ancient, rr_simulation_team_id_players);
        EntityIdx hitter = rr_simulation_alloc_mob(sim(), 1, 9100, 9000, rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
        struct rr_component_health *health = rr_simulation_get_health(sim(), victim);
        rr_component_health_set_max_health(health, 100000);
        rr_component_health_set_health(health, 100000);
        health->nazar_charges = RR_NAZAR_CHARGES;
        float before = health->health;
        // hits that don't kill do their full damage and don't use a charge
        for (uint8_t i = 0; i < 5; ++i)
            rr_component_health_do_damage(sim(), health, hitter, 1000, 0);
        CHECK(fabsf((before - health->health) - 5000) < 1, "hits that don't kill are not touched (%.0f)", before - health->health);
        CHECK(health->nazar_charges == RR_NAZAR_CHARGES, "and use no charge");
        // killing hits: at 500 health, 1000 damage
        float lost[5];
        for (uint8_t i = 0; i < 4; ++i)
        {
            rr_component_health_set_health(health, 500);
            rr_component_health_do_damage(sim(), health, hitter, 1000, 0);
            lost[i] = 500 - health->health;
        }
        printf("  killing hits of 1000 at 500 health did %.0f %.0f %.0f %.0f\n", lost[0], lost[1], lost[2], lost[3]);
        float blocked = 1000 * (1 - RR_NAZAR_BLOCK);
        CHECK(fabsf(lost[0] - blocked) < 1 && fabsf(lost[1] - blocked) < 1 && fabsf(lost[2] - blocked) < 1,
              "the first three killing hits do 2%%");
        CHECK(health->health == 0 && fabsf(lost[3] - 500) < 1, "the fourth kills");
        CHECK(health->nazar_charges == 0, "no charges are left");
        // the first one is dead, a new one for the rest
        victim = rr_simulation_alloc_mob(sim(), 1, 9000, 9100, rr_mob_id_ant, rr_rarity_id_ancient, rr_simulation_team_id_players);
        health = rr_simulation_get_health(sim(), victim);
        rr_component_health_set_max_health(health, 100000);
        // a hit so big that 2% of it still kills leaves 1 health
        rr_component_health_set_health(health, 500);
        health->nazar_charges = 1;
        rr_component_health_do_damage(sim(), health, hitter, 1e9f, 0);
        CHECK(health->health == 1, "an enormous hit leaves 1 health, got %.1f", health->health);
        // armor first: a hit armor takes down to not killing is not saved
        rr_component_health_set_health(health, 500);
        health->nazar_charges = 1;
        health->damage_reduction_ratio = 0.5f;
        rr_component_health_do_damage(sim(), health, hitter, 800, 0);
        CHECK(fabsf(health->health - 100) < 1 && health->nazar_charges == 1, "armor that is enough keeps the charge (%.0f)", health->health);
        // damage that ignores armor is saved from too
        health->damage_reduction_ratio = 0;
        rr_component_health_set_health(health, 500);
        health->flags |= 4;
        rr_component_health_do_damage(sim(), health, hitter, 1000, 0);
        CHECK(health->nazar_charges == 0 && health->health > 0, "armor ignoring killing hits are saved from too");
    }

    printf("a nazar's saves come back one at a time\n");
    {
        EntityIdx victim = rr_simulation_alloc_mob(sim(), 1, 9000, 9300, rr_mob_id_ant, rr_rarity_id_ancient, rr_simulation_team_id_players);
        struct rr_component_health *health = rr_simulation_get_health(sim(), victim);
        uint16_t reload = RR_NAZAR_RELOAD_TICKS[rr_rarity_id_exotic]; // 4.5 s
        health->nazar_max = RR_NAZAR_CHARGES;
        health->nazar_charges = 0;
        health->nazar_reload_ticks = 0;
        uint32_t back_at[RR_NAZAR_CHARGES] = {0}, t = 0;
        for (uint8_t seen = 0; seen < RR_NAZAR_CHARGES && t < 10 * reload; ++t)
        {
            uint8_t before = health->nazar_charges;
            rr_component_health_recharge_nazar(health, reload);
            if (health->nazar_charges > before)
                back_at[seen++] = t + 1;
        }
        CHECK(back_at[0] == reload && back_at[1] == 2 * reload && back_at[2] == 3 * reload,
              "from none, one save is back after %u, %u and %u ticks (got %u %u %u)", reload, 2 * reload, 3 * reload, back_at[0], back_at[1], back_at[2]);
        CHECK(health->nazar_charges == RR_NAZAR_CHARGES, "all three after three reloads (13.5 s for an exotic nazar)");
        // full: the timer rests, and a used save starts it from the beginning
        for (uint16_t i = 0; i < 1000; ++i)
            rr_component_health_recharge_nazar(health, reload);
        CHECK(health->nazar_charges == RR_NAZAR_CHARGES && health->nazar_reload_ticks == 0, "a full nazar stays full with the timer at 0");
        health->nazar_charges = RR_NAZAR_CHARGES - 1;
        for (uint16_t i = 0; i < reload - 1; ++i)
            rr_component_health_recharge_nazar(health, reload);
        CHECK(health->nazar_charges == RR_NAZAR_CHARGES - 1, "one tick before the reload nothing is back");
        // another save used while one is on its way does not restart the timer
        health->nazar_charges = RR_NAZAR_CHARGES - 2;
        rr_component_health_recharge_nazar(health, reload);
        CHECK(health->nazar_charges == RR_NAZAR_CHARGES - 1, "the running timer is kept when another save is used");
    }

    printf("better nazars use their charges up less often\n");
    {
        for (uint8_t rarity = 0; rarity < rr_rarity_id_max; ++rarity)
        {
            EntityIdx victim = rr_simulation_alloc_mob(sim(), 1, 9000, 9200, rr_mob_id_ant, rr_rarity_id_ancient, rr_simulation_team_id_players);
            EntityIdx hitter = rr_simulation_alloc_mob(sim(), 1, 9100, 9200, rr_mob_id_ant, rr_rarity_id_common, rr_simulation_team_id_mobs);
            struct rr_component_health *health = rr_simulation_get_health(sim(), victim);
            rr_component_health_set_max_health(health, 100000);
            health->nazar_rarity = rarity;
            uint32_t saves = 4000, used = 0;
            for (uint32_t i = 0; i < saves; ++i)
            {
                rr_component_health_set_health(health, 500);
                health->nazar_charges = 5;
                rr_component_health_do_damage(sim(), health, hitter, 1000, 0);
                used += 5 - health->nazar_charges;
                if (health->health == 0)
                    break;
            }
            float percent = 100.0f * used / saves;
            printf("  %s nazar used a charge in %.1f%% of saves (table %.1f%%)\n", RR_RARITY_NAMES[rarity], percent, RR_NAZAR_CONSUME_PERCENT[rarity]);
            CHECK(fabsf(percent - RR_NAZAR_CONSUME_PERCENT[rarity]) < 3, "close to the table");
            CHECK(health->health > 0, "and it always saves");
        }
        CHECK(RR_NAZAR_CONSUME_PERCENT[0] == 100 && RR_NAZAR_CONSUME_PERCENT[rr_rarity_id_ancient] == 60, "100%% for common, 60%% for ancient");
    }


    printf("the beetle and the mantis are on the end of the mob list, the others keep their ids\n");
    {
        CHECK(rr_mob_id_snail == 27 && rr_mob_id_beetle == 28 && rr_mob_id_mantis == 29 && rr_mob_id_max == 30,
              "ids 0 to 27 are the ones that were, the new ones follow");
        uint32_t wrong = 0, unnamed = 0;
        for (uint32_t id = 0; id < rr_mob_id_max; ++id)
        {
            wrong += RR_MOB_DATA[id].id != id;
            unnamed += RR_MOB_NAMES[id] == NULL || RR_MOB_NAMES[id][0] == 0;
        }
        CHECK(wrong == 0 && unnamed == 0, "every id has its own row and name (%u wrong, %u unnamed)", wrong, unnamed);
        CHECK(strcmp(RR_MOB_NAMES[rr_mob_id_beetle], "Beetle") == 0 &&
                  strcmp(RR_MOB_NAMES[rr_mob_id_mantis], "Mantis") == 0,
              "named");
        for (uint8_t id = rr_mob_id_beetle; id <= rr_mob_id_mantis; ++id)
        {
            for (uint8_t rarity = 0; rarity < rr_rarity_id_max; ++rarity)
            {
                EntityIdx mob = rr_simulation_alloc_mob(sim(), 1, 9000, 9000, id, rarity, rr_simulation_team_id_mobs);
                CHECK(rr_simulation_get_health(sim(), mob)->max_health > 0 && rr_simulation_get_mob(sim(), mob)->id == id,
                      "%s of rarity %u is made with health", RR_MOB_NAMES[id], rarity);
                rr_simulation_request_entity_deletion(sim(), mob);
            }
            tick(30);
        }
        // the garden's table gives them, and is still a table that ends at 1
        double top = RR_GARDEN_MOB_ID_RARITY_COEFFICIENTS[rr_mob_id_max - 1];
        CHECK(top > 0.999 && top < 1.001, "the garden table ends at 1 (%f)", top);
        uint32_t seen[rr_mob_id_max] = {0};
        for (uint32_t i = 0; i < 20000; ++i)
            ++seen[get_spawn_id(rr_biome_id_garden, NULL)];
        CHECK(seen[rr_mob_id_beetle] > 0 && seen[rr_mob_id_mantis] > 0,
              "the garden spawns both (%u beetles, %u mantises of 20000)", seen[rr_mob_id_beetle], seen[rr_mob_id_mantis]);
        CHECK(seen[rr_mob_id_ant] > 0 && seen[rr_mob_id_spider] > 0, "and the ants and spiders still");
        uint32_t hell_new = 0;
        for (uint32_t i = 0; i < 20000; ++i)
        {
            uint8_t hid = get_spawn_id(rr_biome_id_hell_creek, NULL);
            hell_new += hid == rr_mob_id_beetle || hid == rr_mob_id_mantis;
        }
        CHECK(hell_new == 0, "hell creek does not (%u)", hell_new);
    }

    printf("a player's tag: the skin of the flower, and the old master and old data\n");
    {
        CHECK(rr_flower_skin_from_tag("Spider") == rr_flower_skin_spider && rr_flower_skin_from_tag("spider") == rr_flower_skin_spider &&
                  rr_flower_skin_from_tag("SPIDER") == rr_flower_skin_spider,
              "Spider, in any case, is the spider skin");
        CHECK(rr_flower_skin_from_tag("") == rr_flower_skin_normal && rr_flower_skin_from_tag("Supporter") == rr_flower_skin_normal &&
                  rr_flower_skin_from_tag("Spiders") == rr_flower_skin_normal && rr_flower_skin_from_tag("Spide") == rr_flower_skin_normal &&
                  rr_flower_skin_from_tag(NULL) == rr_flower_skin_normal,
              "no tag, other tags, and near misses are the normal flower");
        CHECK(rr_tag_flips_flower("Supporter") == 1 && rr_tag_flips_flower("") == 0 && rr_tag_flips_flower("Spider") == 0,
              "a name only tag still turns the flower round, none and the skin do not");
        // the flower sends its skin when it has one, and not otherwise
        EntityIdx f = rr_simulation_alloc_entity(sim());
        struct rr_component_flower *flower = rr_simulation_add_flower(sim(), f);
        CHECK(flower->skin == rr_flower_skin_normal, "a flower starts normal");
        CHECK(flower_state_high_bit(flower, 0) == 0, "and an unchanged one sends nothing about it");
        rr_component_flower_set_skin(flower, rr_flower_skin_spider);
        CHECK(flower_state_high_bit(flower, 0) == 1, "setting the spider skin is sent");
        CHECK(flower_state_high_bit(flower, 1) == 1, "and so is a new flower");
        rr_simulation_request_entity_deletion(sim(), f);
        tick(2);

        // what the master says when a player joins
        static uint8_t message[512];
        struct rr_server_client *c = &server.clients[5];
        uint8_t gallery_beetle = rr_mob_id_beetle + 1;
        c->nonce = 99;
        size_t size = fake_join(message, 99, gallery_beetle, "Spider");
        struct rr_binary_encoder d;
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && strcmp(c->tag, "Spider") == 0 && strcmp(c->saved_nickname, "Nick") == 0,
              "the tag after the nickname is read (%s)", c->tag);
        CHECK(c->mob_gallery[rr_mob_id_beetle][rr_rarity_id_rare] == 7 && c->inventory[rr_petal_id_basic][0] == 5, "with the rest as before");
        size = fake_join(message, 99, gallery_beetle, NULL);
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && c->tag[0] == 0 && strcmp(c->saved_nickname, "Nick") == 0,
              "a master from before the tags ends at the nickname: no tag, the rest as it was");
        // a message of the master and a stale buffer after it: the end of the message is the end
        memset(message + size, 'x', 40);
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && c->tag[0] == 0, "what is after the end is not read");
        // an older save knows fewer mobs: the ones it has are kept; a mob this server does not know is left out
        size = fake_join(message, 99, rr_mob_id_snail + 1, "");
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && c->mob_gallery[rr_mob_id_snail][rr_rarity_id_rare] == 7 &&
                  c->mob_gallery[rr_mob_id_beetle][rr_rarity_id_rare] == 0 && c->tag[0] == 0,
              "a save from before the new mobs reads as it did");
        size = fake_join(message, 99, 200, "Spider");
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && strcmp(c->tag, "Spider") == 0, "a mob id from a newer game is skipped, not written out of the table");
        // a tag that is too long is cut to what a tag holds
        size = fake_join(message, 99, gallery_beetle, "AVeryLongTagNameThatDoesNotFit");
        rr_binary_encoder_init(&d, message);
        CHECK(rr_server_client_read_from_api(c, &d, size) == 1 && strlen(c->tag) == RR_TAG_SIZE - 1, "cut to %d letters (%zu)", RR_TAG_SIZE - 1, strlen(c->tag));
        // the nonce is the one the game server gave: another one is turned away
        rr_binary_encoder_init(&d, message);
        c->nonce = 100;
        CHECK(rr_server_client_read_from_api(c, &d, size) == 0, "a message for another nonce is refused");
        memset(c, 0, sizeof *c);
    }

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
