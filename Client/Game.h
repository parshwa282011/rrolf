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

#include <Client/Particle.h>
#include <Client/Renderer/Renderer.h>
#include <Client/Socket.h>
#include <Client/Ui/Ui.h>
#include <Shared/Ctf.h>
#include <Shared/Entity.h>
#include <Shared/Rivet.h>
#include <Shared/Squad.h>
#include <Shared/Snail.h>
#include <Shared/StaticData.h>
#include <Shared/pb.h>

#define RR_DEBUG_POLL_SIZE 10

// the buttons the first-time tutorial points at
enum rr_tutorial_target
{
    rr_tutorial_target_join,
    rr_tutorial_target_create_squad,
    rr_tutorial_target_find_squad,
    rr_tutorial_target_inventory,
    rr_tutorial_target_gallery,
    rr_tutorial_target_crafting,
    rr_tutorial_target_account,
    rr_tutorial_target_cg_login,
    rr_tutorial_target_cg_link,
    rr_tutorial_target_discord_link,
    rr_tutorial_target_max
};

enum rr_game_menu
{
    rr_game_menu_none,
    rr_game_menu_inventory,
    rr_game_menu_gallery,
    rr_game_menu_crafting,
    rr_game_menu_settings,
    rr_game_menu_account,
    rr_game_menu_dev_squad_panel,
    rr_game_menu_legal,
    rr_game_menu_give_petals,
    rr_game_menu_ctf_host
};

enum rr_game_cursor
{
    rr_game_cursor_default,
    rr_game_cursor_pointer
};

struct rr_game_debug_info
{
    uint8_t frame_pos;
    uint8_t websocket_pos;
    long tick_times[RR_DEBUG_POLL_SIZE];
    long frame_times[RR_DEBUG_POLL_SIZE];
    long message_sizes[RR_DEBUG_POLL_SIZE];
};

struct rr_game_crafting_data
{
    float animation;
    float autocraft_animation;
    uint64_t count;
    uint64_t success_count;
    uint32_t last_attempts;
    uint8_t crafting_id;
    uint8_t crafting_rarity;
    uint64_t temp_successes;
    uint64_t temp_fails;
    uint32_t temp_attempts;
    double temp_xp;
    uint8_t autocraft;
};

// what the server last said about the capture the flag match, see Client/Ctf.c
struct rr_game_ctf_team
{
    uint8_t eliminated;
    uint8_t squad;
    uint8_t members;
    uint8_t held;
    uint8_t banked;
    uint8_t point_set[RR_CTF_FLAGS_PER_TEAM];
    float point_x[RR_CTF_FLAGS_PER_TEAM];
    float point_y[RR_CTF_FLAGS_PER_TEAM];
    uint8_t setup[RR_CTF_FLAGS_PER_TEAM];
};

struct rr_game_ctf_flag
{
    uint8_t state;
    uint8_t holder;
    float x;
    float y;
    // what is drawn, follows x and y
    float lerp_x;
    float lerp_y;
};

struct rr_game_ctf_feed_line
{
    char text[96];
    uint32_t color;
    float age;
};

struct rr_game_snail_event
{
    uint8_t type;
    uint8_t rarity;
    uint8_t count;
};

// one snail on its way
struct rr_game_snail_run
{
    uint8_t rarity;
    uint16_t route_cells;
    uint16_t cells_left;
    uint8_t hp;
    uint8_t waiting; // asleep until a player comes close
    float x;
    float y;
};

// the snail run, see Client/Snail.c
struct rr_game_snail
{
    // a state packet has arrived on this connection
    uint8_t active;
    uint8_t run_count;
    struct rr_game_snail_run runs[RR_SNAIL_MAX_RUNS];
    float hole_x;
    float hole_y;
    uint16_t event_seq;
    uint8_t event_seq_known;
    struct rr_game_ctf_feed_line feed[3];
    float anim; // seconds, for things that pulse
};

struct rr_game_ctf_event
{
    uint8_t type;
    uint8_t a;
    uint8_t b;
    uint8_t flag;
};

struct rr_game_ctf
{
    // a state packet has arrived on this connection
    uint8_t active;
    uint8_t phase;
    uint8_t team_count;
    uint8_t my_team;
    uint8_t walls_dropped;
    uint8_t is_host;
    uint8_t can_start;
    uint8_t has_body;
    uint8_t is_leader;
    uint8_t flags_ready;
    uint8_t flags_drawn; // the flags have had a position to start from
    uint8_t winner;
    uint8_t message;
    uint8_t message_arg;
    uint32_t phase_ticks;
    uint16_t event_seq;
    uint8_t event_seq_known;
    struct rr_game_ctf_event events[RR_CTF_EVENT_HISTORY];
    struct rr_game_ctf_team teams[RR_CTF_MAX_TEAMS];
    struct rr_game_ctf_flag flags[RR_CTF_MAX_TEAMS * RR_CTF_FLAGS_PER_TEAM];
    struct rr_game_ctf_feed_line feed[5];
    // host panel
    char codes[RR_CTF_MAX_TEAMS - 1][16];
    float start_confirm; // seconds left of "press start again"
    uint8_t place_cycle; // which flag point F moves once all are placed
    uint32_t loadout_sent; // hash of the loadout the server was last sent
    float loadout_wait;
    float anim; // seconds, for things that pulse
};

struct rr_game_chat_message
{
    uint8_t type;
    char sender_name[16];
    char message[64];
    char text[100];
};

struct rr_game_chat
{
    uint8_t chat_active; // 0 = inactive, 1 = local, 2 = squad
    uint8_t chat_type;
    uint32_t at;
    struct rr_game_chat_message messages[10];
    char sending[64];
};

// anything cross reload
struct rr_game_cache
{
    double experience;
    uint64_t mob_kills[rr_mob_id_max][rr_rarity_id_max];
    struct rr_id_rarity_pair loadout[RR_MAX_SLOT_COUNT * 2];
    uint8_t use_mouse;
    uint8_t displaying_debug_information;
    uint8_t tint_petals;
    uint8_t screen_shake;
    uint8_t low_performance_mode;
    uint8_t disable_chat;
    uint8_t show_coordinates;
    uint8_t show_hitboxes;
    uint8_t hide_ui;
    uint8_t hold_attack;
    uint8_t hold_defense;
    uint8_t show_loot;
    uint8_t disable_leave_hotkey;
    char nickname[128];
};

struct rr_leaderboard_entry
{
    char nickname[16];
    uint64_t points;
};

struct rr_game_squad
{
    struct rr_squad_member squad_members[RR_SQUAD_MEMBER_COUNT];
    uint8_t squad_index;
    uint8_t squad_owner;
    uint8_t squad_pos;
    uint8_t squad_private;
    uint8_t squad_expose_code;
    char squad_code[16];
};

struct rr_dev_cheats
{
    float summon_mob_id;
    char summon_mob_id_text[32];
    float summon_mob_rarity;
    char summon_mob_rarity_text[16];
    uint8_t invisible;
    uint8_t invulnerable;
    uint8_t no_aggro;
    uint8_t no_wall_collision;
    uint8_t no_collision;
    uint8_t no_grid_influence;
    float speed_percent;
    float fov_percent;
    uint8_t flags_last_tick;
    float speed_percent_last_tick;
    float fov_percent_last_tick;
};

struct rr_game
{
    struct rr_particle_manager default_particle_manager;
    struct rr_particle_manager foreground_particle_manager;
    struct rr_particle_manager crafting_particle_manager;
    struct rr_game_crafting_data crafting_data;
    struct rr_game_debug_info debug_info;
    struct rr_game_chat chat;
    struct rr_game_ctf ctf;
    struct rr_game_snail snail;
    struct rr_game_cache cache;
    struct rr_game_squad squad;
    struct rr_dev_cheats dev_cheats;
    struct rr_game_squad other_squads[RR_SQUAD_COUNT];

    struct rr_rivet_account rivet_account;
    struct rr_websocket socket;

    struct rr_ui_element *petal_tooltips[rr_petal_id_max][rr_rarity_id_max];
    struct rr_ui_element *mob_tooltips[rr_mob_id_max][rr_rarity_id_max];
    struct rr_ui_element *link_account_tooltip;
    struct rr_ui_element *inventory_tooltip;
    struct rr_ui_element *gallery_tooltip;
    struct rr_ui_element *craft_tooltip;
    struct rr_ui_element *settings_tooltip;
    struct rr_ui_element *account_tooltip;
    struct rr_ui_element *squads_tooltip;
    struct rr_ui_element *discord_tooltip;
    struct rr_ui_element *github_tooltip;
    struct rr_ui_element *gardn_tooltip;
    struct rr_ui_element *fullscreen_tooltip;
    struct rr_ui_element *abandon_game_tooltip;
    struct rr_ui_element *leave_squad_tooltip;
    struct rr_ui_element *click_to_copy_tooltip;
    struct rr_ui_element *kick_from_squad_tooltip;
    struct rr_ui_element *vote_for_kick_tooltip;
    struct rr_ui_element *block_in_chat_tooltip;
    struct rr_ui_element *unblock_in_chat_tooltip;
    struct rr_ui_element *transfer_ownership_tooltip;
    struct rr_ui_element *squad_owner_tooltip;
    struct rr_ui_element *experience_tooltip;
    struct rr_ui_element *link_reminder_tooltip;
    struct rr_ui_element *window;
    struct rr_ui_element *prev_focused;
    struct rr_ui_element *focused;
    struct rr_ui_element *pressed;
    struct rr_ui_element *anti_afk;

    struct rr_renderer *renderer;
    struct rr_input_data *input_data;
    struct rr_simulation *simulation;
    struct rr_simulation *deletion_simulation;
    struct rr_component_player_info *player_info;

    uint64_t inventory[rr_petal_id_max][rr_rarity_id_max];
    uint32_t failed_crafts[rr_petal_id_max][rr_rarity_id_max];
    uint8_t loadout_counts[rr_petal_id_max][rr_rarity_id_max];
    EntityIdx player_infos[RR_SQUAD_MEMBER_COUNT];
    EntityIdx arena_player_infos[RR_MAX_CLIENT_COUNT];
    uint64_t dev_flag;
    float lerp_delta;
    uint16_t afk_ticks;

    struct rr_leaderboard_entry leaderboard[10];
    uint8_t leaderboard_count;
    uint32_t leaderboard_own_rank;
    uint64_t leaderboard_own_points;

    uint8_t socket_ready : 1;
    uint8_t socket_pending : 1;
    uint8_t joined_squad : 1;
    uint8_t simulation_ready : 1;
    uint8_t flower_dead : 1;
    uint8_t block_ui_input : 1;
    uint8_t block_fov_adjustment : 1;
    uint8_t is_mobile : 1;
    uint8_t logged_in : 1;
    uint8_t account_linked : 1;
    uint8_t cg_linked : 1;
    uint8_t cg_logged_in : 1;
    // the server has answered the login, so squad requests are accepted
    uint8_t account_ready : 1;
    uint8_t is_dev : 1;
    uint8_t text_input_focused : 1;
    uint8_t socket_error;
    uint8_t menu_open;
    uint8_t ticks_until_text_cache;
    uint8_t selected_biome;
    uint8_t slots_unlocked;
    uint8_t cursor;
    uint8_t significant_rarity;
    int8_t kick_vote_pos;

    char rivet_player_token[400];
    char connect_code[16];
    // the logged in CrazyGames user, filled in by rr_platform_tick
    char cg_username[64];
    char cg_label[96];
    char afk_challenge[7];
    char auth_message[96];
    // PDEV builds: the dev panel behind the + button is opened with a PIN, which
    // is kept here and sent with every cheat. Nothing is trusted on this side,
    // the game server checks the PIN each time (see rr_dev_cheat_unlock)
    char dev_pin[16];
    uint8_t dev_unlocked;
    char transfer_code[24];
    char enter_code[16];
    // an account request waiting for the connection, and the account to
    // reconnect with after one that changed it, see Oauth2.c
    uint8_t auth_pending;
    uint8_t auth_kind;
    // reconnected after an account change, waiting for the account to load
    uint8_t auth_reconnected;
    float auth_wait;
    float auth_reconnect_in;
    char auth_payload[2048];
    // how long the connection has been verified, and how many in a row were
    // dropped within seconds, see the reconnect in rr_game_tick
    float socket_lifetime;
    float reconnect_wait;
    uint8_t quick_closes;
    // first-time tutorial, see Ui/InGame/Tutorial.c
    struct
    {
        // where the player is in the tutorial, remembered in localStorage
        uint8_t stage;
        uint8_t has_start;
        uint8_t saw_craft;
        uint8_t craft_id;
        uint8_t craft_rarity;
        float progress;
        float start_x;
        float start_y;
        float pulse;
        // the buttons the tutorial can point at, see rr_tutorial_target
        struct rr_ui_element *targets[rr_tutorial_target_max];
    } tutorial;
};

void rr_game_init(struct rr_game *);
void rr_game_tick(struct rr_game *, float);
void rr_game_connect_socket(struct rr_game *);
void rr_game_select_biome(struct rr_game *, uint8_t);
// code is a squad code with the name of its server in front ("hc-abcdef"),
// which switches to that server if needed
void rr_game_join_squad_by_code(struct rr_game *, char const *code);
void rr_simulation_read_binary(struct rr_game *, struct proto_bug *);
void rr_write_dev_cheat_packets(struct rr_game *, uint8_t);
// starts a dev cheat packet: the header and the type, and in a PDEV build the
// PIN, which the game server wants with every cheat. The caller adds the rest
struct proto_bug;
void rr_dev_cheat_begin(struct rr_game *, struct proto_bug *, uint8_t type);

void rr_game_websocket_on_event_function(enum rr_websocket_event_type, void *,
                                         void *, uint64_t);

uint64_t rr_game_get_adjusted_inventory_count(struct rr_game *, uint8_t,
                                              uint8_t);
