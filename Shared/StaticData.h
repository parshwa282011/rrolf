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

#pragma once

#include <stdint.h>

#include <Shared/Entity.h>

enum rr_animation_type
{
    rr_animation_type_default = 0,
    rr_animation_type_lightningbolt = 1,
    rr_animation_type_damagenumber = 2,
    rr_animation_type_chat = 3,
};

enum rr_serverbound_packet_header
{
    rr_serverbound_input,
    rr_serverbound_petal_switch,
    rr_serverbound_squad_join,
    rr_serverbound_squad_ready,
    rr_serverbound_squad_update,
    rr_serverbound_private_update,
    rr_serverbound_squad_kick,
    rr_serverbound_petals_craft,
    rr_serverbound_chat,

    // cheats
    rr_serverbound_dev_summon,
    rr_serverbound_dev_give_petal,
    rr_serverbound_dev_set_slot_count,
    rr_serverbound_dev_summon_portal,
};

enum rr_clientbound_packet_header
{
    rr_clientbound_update,
    rr_clientbound_animation_update,
    rr_clientbound_squad_dump,
    rr_clientbound_squad_fail,
    rr_clientbound_squad_leave,
    rr_clientbound_account_result,
    rr_clientbound_craft_result,
    rr_clientbound_redirect
};

#define RR_SLOT_COUNT_FROM_LEVEL(level) (level < 100 ? 5 + (level) / 20 : 10)
#define RR_PLAYER_SPEED (4.0f)
// portal radius at common rarity; scaled per-rarity via RR_MOB_RARITY_SCALING
#define RR_PORTAL_BASE_RADIUS (80.0f)

enum rr_biome_id
{
    rr_biome_id_hell_creek_easy,
    rr_biome_id_hell_creek_med,
    rr_biome_id_garden,
    rr_biome_id_beehive,
    rr_biome_id_max
};

enum rr_rarity_id
{
    rr_rarity_id_common,
    rr_rarity_id_unusual,
    rr_rarity_id_rare,
    rr_rarity_id_epic,
    rr_rarity_id_legendary,
    rr_rarity_id_mythic,
    rr_rarity_id_exotic,
    rr_rarity_id_ultimate,
    rr_rarity_id_divine,
    rr_rarity_id_celestial,
    rr_rarity_id_primal,
    rr_rarity_id_ancient,
    rr_rarity_id_arcane,
    rr_rarity_id_radiant,
    rr_rarity_id_astral,
    rr_rarity_id_ethereal,
    rr_rarity_id_eternal,
    rr_rarity_id_cosmic,
    rr_rarity_id_max
};

enum rr_petal_id
{
    rr_petal_id_none,
    rr_petal_id_basic,
    rr_petal_id_pellet,
    rr_petal_id_fossil,
    rr_petal_id_stinger,
    rr_petal_id_light,
    rr_petal_id_shell,
    rr_petal_id_peas,
    rr_petal_id_leaf,
    rr_petal_id_egg,
    rr_petal_id_magnet,
    rr_petal_id_uranium,
    rr_petal_id_feather,
    rr_petal_id_azalea,
    rr_petal_id_bone,
    rr_petal_id_web,
    rr_petal_id_seed,
    rr_petal_id_gravel,
    rr_petal_id_club,
    rr_petal_id_crest,
    rr_petal_id_droplet,
    rr_petal_id_beak,
    rr_petal_id_lightning,
    rr_petal_id_third_eye,
    rr_petal_id_mandible,
    rr_petal_id_wax,
    rr_petal_id_sand,
    rr_petal_id_mint,

    rr_petal_id_max
};

enum rr_mob_id
{
    rr_mob_id_triceratops,
    rr_mob_id_trex,
    rr_mob_id_fern,
    rr_mob_id_tree,
    rr_mob_id_pteranodon,
    rr_mob_id_dakotaraptor,
    rr_mob_id_pachycephalosaurus,
    rr_mob_id_ornithomimus,
    rr_mob_id_ankylosaurus,
    rr_mob_id_meteor,
    rr_mob_id_quetzalcoatlus,
    rr_mob_id_edmontosaurus,

    rr_mob_id_ant,
    rr_mob_id_hornet,
    rr_mob_id_dragonfly,
    rr_mob_id_honeybee,
    rr_mob_id_beehive,
    rr_mob_id_spider,
    rr_mob_id_house_centipede,
    rr_mob_id_lanternfly,
    rr_mob_id_max
};

struct rr_petal_base_stat_scale
{
    float health;
    float damage;
};

struct rr_loot_data
{
    uint8_t id;
    float seed;
};

struct rr_mob_data
{
    uint8_t id;
    uint8_t min_rarity;
    uint8_t max_rarity;
    float health;
    float damage;
    float radius;
    struct rr_loot_data loot[4];
};

struct rr_petal_data
{
    uint8_t id;
    uint8_t min_rarity; // minimum rarity petal can spawn at
    struct rr_petal_base_stat_scale const *scale;
    float damage;
    float health;
    float clump_radius;
    uint32_t cooldown;
    uint32_t secondary_cooldown; // for stuff like projectiles
    uint8_t count[rr_rarity_id_max];
};

struct rr_petal_rarity_scale
{
    float heal;
    float seed_cooldown;
    float web_radius;
};

struct rr_mob_rarity_scale
{
    float health;
    float damage;
    float radius;
};

extern struct rr_petal_data RR_PETAL_DATA[rr_petal_id_max];
extern char const *RR_PETAL_NAMES[rr_petal_id_max];
extern char const *RR_PETAL_DESCRIPTIONS[rr_petal_id_max];
extern struct rr_mob_data RR_MOB_DATA[rr_mob_id_max];
extern char const *RR_MOB_NAMES[rr_mob_id_max];
extern struct rr_mob_rarity_scale RR_MOB_RARITY_SCALING[rr_rarity_id_max];
extern struct rr_petal_rarity_scale RR_PETAL_RARITY_SCALE[rr_rarity_id_max];
extern double RR_MOB_LOOT_RARITY_COEFFICIENTS[rr_rarity_id_max];
extern double RR_DROP_RARITY_COEFFICIENTS[rr_rarity_id_max];
extern double RR_MOB_WAVE_RARITY_COEFFICIENTS[rr_rarity_id_max + 1];

extern uint32_t RR_MOB_DIFFICULTY_COEFFICIENTS[rr_mob_id_max];
extern double RR_HELL_CREEK_EASY_MOB_ID_RARITY_COEFFICIENTS[rr_mob_id_max];
extern double RR_HELL_CREEK_MED_MOB_ID_RARITY_COEFFICIENTS[rr_mob_id_max];
extern double RR_GARDEN_MOB_ID_RARITY_COEFFICIENTS[rr_mob_id_max];

extern uint32_t RR_RARITY_COLORS[rr_rarity_id_max];
extern char const *RR_RARITY_NAMES[rr_rarity_id_max];

struct rr_maze_grid
{
#ifdef RR_SERVER
    uint8_t (*spawn_function)();
    uint32_t spawn_timer;
    uint32_t player_count;
    // recomputed from scratch every tick (see despawn_mob in
    // Server/Simulation.c) from mobs currently within FOV of this cell, so
    // a mob that wandered off-screen stops counting against the cap here
    // instead of holding it down until it despawns
    uint32_t live_points;
    float local_difficulty;
    float overload_factor;
    // tick id (see Server/Simulation.c's current_maze_tick) this cell's
    // player_count/local_difficulty/live_points were last reset and touched
    // on - lets tick_maze() lazily "clear" only cells it actually visits
    // instead of sweeping the whole maze every tick
    uint32_t touch_tick;
#endif
    uint8_t value;
    float difficulty;
};

struct rr_spawn_zone
{
    float x;
    float y;
};

// a discoverable respawn waypoint (cloned from rysteria,
// github.com/maxnest0x0/rysteria): walking into the (x, y, w, h) box (in
// half-resolution template coordinates, like rr_spawn_zone) while at or
// above min_level moves the player's respawn point to (spawn_x, spawn_y).
// only used by biomes with checkpoint_count > 0 - see
// Server/System/Checkpoints.c; others keep using spawn_zones above.
struct rr_checkpoint
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t spawn_x;
    uint32_t spawn_y;
    uint32_t min_level;
};

struct rr_maze_declaration
{
    uint32_t maze_dim;
    float grid_size;
    struct rr_maze_grid *maze;
    struct rr_spawn_zone spawn_zones[4];
    uint8_t checkpoint_count;
    struct rr_checkpoint checkpoints[9];
};

#define RR_DECLARE_MAZE(name, size)                                            \
    extern uint8_t RR_MAZE_TEMPLATE_##name[size / 2][size / 2];                \
    extern struct rr_maze_grid RR_MAZE_##name[size][size];

// RR_DECLARE_MAZE(HELL_CREEK_EASY, 54)
RR_DECLARE_MAZE(HELL_CREEK_EASY, 80)
RR_DECLARE_MAZE(HELL_CREEK_MED, 240)
RR_DECLARE_MAZE(BURROW, 4)

extern struct rr_maze_declaration RR_MAZES[rr_biome_id_max];

extern uint8_t RR_GLOBAL_BIOME;

extern double RR_BASE_CRAFT_CHANCES[rr_rarity_id_max - 1];
extern double RR_CRAFT_CHANCES[rr_rarity_id_max - 1];
// xp granted per craft attempt made *from* a given rarity (indexed by the
// source rarity being crafted up from); used by both the server (actually
// grants it) and the client (Crafting.c, to display it).
extern double CRAFT_XP_GAINS[rr_rarity_id_max - 1];

void rr_static_data_init();

double xp_to_reach_level(uint32_t);
uint32_t level_from_xp(double);