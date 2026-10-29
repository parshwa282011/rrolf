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
// new ones, killing the queen lets the others out, diggers cut through walls,
// plants leave holes behind at the rate they should. Built with -DGARDEN by
// tools/garden/run-sim-test.sh.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Server/EntityAllocation.h>
#include <Server/Server.h>
#include <Server/Simulation.h>
#include <Server/Snail.h>
#include <Shared/Component/Arena.h>
#include <Shared/Component/Health.h>
#include <Shared/Component/Mob.h>
#include <Shared/StaticData.h>

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

// A hive or hole nobody is near unloads after 30 seconds, and there are no
// players here, so the holes are told that somebody is
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
        uint32_t most_diggers = 0, total_diggers = 0;
        uint32_t trials = 60;
        for (uint32_t t = 0; t < trials; ++t)
        {
            EntityIdx hole = make_hole(rarity);
            struct rr_component_arena *arena =
                rr_simulation_get_arena(sim(), hole);
            uint32_t diggers = count_mobs(hole, rr_mob_id_digger_ant);
            if (t == 0)
            {
                CHECK(arena->biome == rr_biome_id_ant_hole, "the hole has its own biome");
                CHECK(count_mobs(hole, rr_mob_id_queen_ant) == 1, "one queen at rarity %u", rarity);
                CHECK(count_mobs(hole, rr_mob_id_ant_egg) == scale->egg_count,
                      "%u eggs at rarity %u, got %u", scale->egg_count, rarity,
                      count_mobs(hole, rr_mob_id_ant_egg));
                CHECK(arena->ant_egg_count == scale->egg_count, "the egg count is kept (%u)", arena->ant_egg_count);
                CHECK(count_mobs(hole, rr_mob_id_ant) == scale->worker_count, "%u workers at rarity %u", scale->worker_count, rarity);
                CHECK(count_mobs(hole, rr_mob_id_baby_ant) == scale->baby_ant_count, "%u babies at rarity %u", scale->baby_ant_count, rarity);
                uint32_t all = 1 + scale->egg_count + scale->worker_count + scale->baby_ant_count + diggers;
                CHECK(arena->mob_count == all, "mob_count %u is what is in it, %u", arena->mob_count, all);
            }
            if (diggers > most_diggers)
                most_diggers = diggers;
            total_diggers += diggers;
            remove_hole(hole);
        }
        printf("  rarity %u: diggers %.2f on average, at most %u\n", rarity,
               (float)total_diggers / trials, most_diggers);
        CHECK(most_diggers <= 2, "never more than two diggers, got %u", most_diggers);
        if (scale->digger_second_chance == 0)
            CHECK(most_diggers <= 1, "no second digger at rarity %u", rarity);
        float expected = scale->digger_first_chance * (1 + scale->digger_second_chance);
        float average = (float)total_diggers / trials;
        CHECK(fabsf(average - expected) < 0.35f, "average %.2f is close to %.2f", average, expected);
        if (scale->digger_first_chance >= 1)
            CHECK(total_diggers >= trials, "a digger every time at rarity %u", rarity);
    }

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
        uint32_t diggers = count_mobs(hole, rr_mob_id_digger_ant);
        uint32_t outside_before = count_mobs(1, rr_mob_id_ant) + count_mobs(1, rr_mob_id_baby_ant) + count_mobs(1, rr_mob_id_digger_ant);
        kill(find_mob(hole, rr_mob_id_queen_ant));
        tick(3);
        CHECK(!rr_simulation_has_entity(sim(), hole), "the hole is gone");
        CHECK(count_mobs(hole, rr_mob_id_ant_egg) == 0, "no eggs are left");
        uint32_t outside_after = count_mobs(1, rr_mob_id_ant) + count_mobs(1, rr_mob_id_baby_ant) + count_mobs(1, rr_mob_id_digger_ant);
        CHECK(outside_after - outside_before == workers + kids + diggers,
              "%u ants came out, expected %u", outside_after - outside_before, workers + kids + diggers);
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

    printf("a digger cuts through a wall to a target on the other side\n");
    {
        EntityIdx hole = make_hole(rr_rarity_id_rare);
        struct rr_component_arena *arena = rr_simulation_get_arena(sim(), hole);
        // nothing but our two
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
        tick(2);
        float g = arena->maze->grid_size;
        uint32_t dim = arena->maze->maze_dim;
        // two open cells in a line with one or two wall cells between them
        int found = 0;
        float ax = 0, ay = 0, bx = 0, by = 0;
        for (uint32_t y = 1; y < dim - 1 && !found; ++y)
            for (uint32_t x = 1; x < dim - 4 && !found; ++x)
            {
                if (rr_component_arena_get_grid(arena, x, y)->value != 1)
                    continue;
                for (uint32_t gap = 1; gap <= 2 && !found; ++gap)
                {
                    int walls = 1;
                    for (uint32_t k = 1; k <= gap; ++k)
                    {
                        uint8_t w = rr_component_arena_get_grid(arena, x + k, y)->value;
                        walls &= w == 0 || (w & 8);
                    }
                    if (walls &&
                        rr_component_arena_get_grid(arena, x + gap + 1, y)->value == 1)
                    {
                        ax = (x + 0.5f) * g; ay = (y + 0.5f) * g;
                        bx = (x + gap + 1.5f) * g; by = (y + 0.5f) * g;
                        found = 1;
                    }
                }
            }
        CHECK(found, "the burrow has two open cells with a thin wall between them");
        if (found)
        {
            EntityIdx digger = rr_simulation_alloc_mob(sim(), hole, ax, ay, rr_mob_id_digger_ant, rr_rarity_id_legendary, rr_simulation_team_id_mobs);
            ++arena->mob_count;
            EntityIdx target = rr_simulation_alloc_mob(sim(), hole, bx, by, rr_mob_id_snail, rr_rarity_id_legendary, rr_simulation_team_id_players);
            ++arena->mob_count;
            rr_simulation_get_mob(sim(), target)->no_drop = 1;
            struct rr_component_physical *dp = rr_simulation_get_physical(sim(), digger);
            struct rr_component_physical *tp = rr_simulation_get_physical(sim(), target);
            int tunneled = 0, reached = 0;
            for (uint32_t t = 0; t < 8 * 25 && !reached; ++t)
            {
                tick(1);
                if (dp->tunneling)
                    tunneled = 1;
                float dx = dp->x - tp->x, dy = dp->y - tp->y;
                if (dx * dx + dy * dy < 4 * (dp->radius + tp->radius) * (dp->radius + tp->radius))
                    reached = 1;
            }
            CHECK(tunneled, "the digger dug");
            CHECK(reached, "and got to the target, %.0f units away", sqrtf((dp->x - tp->x) * (dp->x - tp->x) + (dp->y - tp->y) * (dp->y - tp->y)));
            tick(4 * 25);
            CHECK(!dp->tunneling || rr_component_arena_get_grid(arena, dp->x / g, dp->y / g)->value != 1, "it never stays dug into a wall");
            uint8_t v = rr_component_arena_get_grid(arena, dp->x / g, dp->y / g)->value;
            CHECK(!dp->tunneling ? 1 : v == 1 || v != 0, "(position is in the maze)");
        }
        remove_hole(hole);
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
            uint8_t min = RR_SNAIL_ZONE_MIN(run->rarity), max = RR_SNAIL_ZONE_MAX(run->rarity);
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

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
