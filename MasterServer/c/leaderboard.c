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

#include "leaderboard.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "jobs.h"
#include "util.h"

// rarities, the order of enum rr_rarity_id in rrolf/Shared/StaticData.h
#define RARITY_MYTHIC 5
#define RARITY_ANCIENT 7

#define TOP_SIZE 10
// the top lists are shared by everybody for this long
#define CACHE_MS 8000

struct board
{
    char const *id;
    char const *game;
    char const *title;
    char const *unit;   // after the number, "" for none
    char const *format; // "level" shows the number of the level only
    char const *note;
};

static struct board const BOARDS[] = {
    {"rr_level", "rysteria", "Level", "", "level",
     "How far the player has levelled"},
    {"rr_pvp_kills", "rysteria", "PvP kills", "", "int",
     "Players killed in the PvP zone"},
    {"rr_pvp_best", "rysteria", "Best PvP score", "", "int",
     "Highest score reached in the PvP zone"},
    {"rr_ancient_attempts", "rysteria", "Ancient craft attempts", "", "int",
     "Tries to craft an Ancient petal, successful or not"},
    {"rr_ancients", "rysteria", "Ancients owned", "", "int",
     "Ancient petals in the inventory right now"},
    {"rr_ancient_kills", "rysteria", "Ancients killed", "", "int",
     "Ancient mobs killed"},
    {"rr_kills", "rysteria", "Mob kills", "", "int",
     "Mobs killed in Parteria"},
    {"gardn_kills", "gardn", "Gardn kills", "", "int", "Kills in gardn"},
    {"gardn_best_score", "gardn", "Highest score", "", "int",
     "Best score of a gardn save"},
    {"gardn_mythics", "gardn", "Mythics in saves", "", "int",
     "Mythic petals in all saves"},
    // minigames (MS_LB_MG_WINS + mode, MS_LB_MG_PLAYED + mode), only counted
    // for matches that were started with the PIN
    {"mg_ctf_wins", "rysteria", "CTF wins", "", "int",
     "Capture the flag matches won"},
    {"mg_tdm_wins", "rysteria", "TDM wins", "", "int",
     "Team deathmatch matches won"},
    {"mg_has_wins", "rysteria", "Hide and seek wins", "", "int",
     "Hide and seek matches won"},
    // not listed on the page, the profile shows them as "won x, lost y"
    {"mg_ctf_played", "hidden", "CTF played", "", "int", ""},
    {"mg_tdm_played", "hidden", "TDM played", "", "int", ""},
    {"mg_has_played", "hidden", "Hide and seek played", "", "int", ""},
};
#define MS_LB_MG_WINS 10
#define MS_LB_MG_PLAYED 13
static char const *const MG_MODES[3] = {"ctf", "tdm", "has"};
static char const *const GAME_IDS[2] = {"rysteria", "gardn"};
#define BOARD_COUNT ((int)(sizeof BOARDS / sizeof *BOARDS))

// ------------------------------------------------------------------ numbers

static double xp_to_reach_level(uint32_t level)
{
    if (level <= 60)
        return (level + 5) * pow(1.175, level);
    double base = (level + 5) * pow(1.175, 60);
    for (uint32_t i = 60; i < level; ++i)
    {
        double f = 1.18 - 0.0075 * (i - 60);
        base *= f < 1.1 ? 1.1 : f > 1.18 ? 1.18 : f;
    }
    return base;
}

// level + how far to the next one (0 to 1 excluded), so that the order is the
// order of xp and equal levels are told apart
static double level_score(double xp)
{
    if (!(xp > 0))
        return 1;
    uint32_t level = 1;
    while (level < 1000 && xp >= xp_to_reach_level(level + 1))
        xp -= xp_to_reach_level(++level);
    double next = xp_to_reach_level(level + 1);
    return level + (next > 0 ? xp / next : 0);
}

static double sum_counts(cJSON const *map, int rarity, double *all)
{
    double total = 0;
    cJSON const *item;
    cJSON_ArrayForEach(item, map)
    {
        unsigned id, r;
        if (!cJSON_IsNumber(item) || item->string == NULL ||
            sscanf(item->string, "%u:%u", &id, &r) != 2 || item->valuedouble < 0)
            continue;
        if (all != NULL)
            *all += item->valuedouble;
        if ((int)r == rarity)
            total += item->valuedouble;
    }
    return total;
}

void ms_lb_parteria_values(struct ms_db_parteria const *rys,
                           double out[MS_LB_PARTERIA_BOARDS])
{
    memset(out, 0, sizeof(double) * MS_LB_PARTERIA_BOARDS);
    out[0] = level_score(rys->xp);
    out[2] = rys->pvp_points > 0 ? (double)rys->pvp_points : 0;
    cJSON *petals = cJSON_Parse(rys->petals ? rys->petals : "{}");
    out[4] = sum_counts(petals, RARITY_ANCIENT, NULL);
    cJSON_Delete(petals);
    cJSON *mobs = cJSON_Parse(rys->mob_kills ? rys->mob_kills : "{}");
    out[5] = sum_counts(mobs, RARITY_ANCIENT, &out[6]);
    cJSON_Delete(mobs);
}

// {"name": ...} as the boards keep it
static char *name_json(char const *username)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", username && username[0] ? username : "?");
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return text;
}

static int apply(int board, char const *uuid, double value,
                 enum ms_lb_mode mode, char const *extra)
{
    return MS_DB->lb_apply(MS_DB, BOARDS[board].id, uuid, value, mode, extra) ==
                   MS_DB_OK
               ? 0
               : -1;
}

int ms_lb_push_parteria(char const *uuid, char const *username,
                        struct ms_db_parteria const *rys, int64_t pvp_best,
                        struct ms_lb_counters const *counts, double const *prev,
                        double now[MS_LB_PARTERIA_BOARDS])
{
    double v[MS_LB_PARTERIA_BOARDS];
    ms_lb_parteria_values(rys, v);
    if ((double)pvp_best > v[2])
        v[2] = (double)pvp_best;
    char *extra = name_json(username);
    int failed = 0;
    for (int b = 0; b < MS_LB_PARTERIA_BOARDS; ++b)
    {
        if (b == 1 || b == 3)
            continue; // counted below
        if (now != NULL)
            now[b] = prev != NULL ? prev[b] : -1;
        // what has not changed is not written again
        if (prev != NULL && prev[b] == v[b])
            continue;
        if (apply(b, uuid, v[b], b == 2 ? ms_lb_max : ms_lb_set, extra) != 0)
            ++failed;
        else if (now != NULL)
            now[b] = v[b];
    }
    if (counts != NULL)
    {
        if (counts->pvp_kills > 0 &&
            apply(1, uuid, (double)counts->pvp_kills, ms_lb_add, extra) != 0)
            ++failed;
        if (counts->attempts > 0 &&
            apply(3, uuid, (double)counts->attempts, ms_lb_add, extra) != 0)
            ++failed;
    }
    free(extra);
    return failed ? -1 : 0;
}

int ms_lb_push_gardn(char const *uuid, uint64_t kills, int have_stats,
                     uint64_t mythics, uint64_t best_score)
{
    struct ms_db_account a;
    if (MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &a) != MS_DB_OK ||
        a.is_dev)
        return 0; // no such account, or one with everything: not on the boards
    char *extra = name_json(a.username);
    int failed = 0;
    int const kills_board = MS_LB_PARTERIA_BOARDS;
    if (kills > 0 && apply(kills_board, uuid, (double)kills, ms_lb_add, extra))
        ++failed;
    if (have_stats)
    {
        if (apply(kills_board + 1, uuid, (double)best_score, ms_lb_max, extra))
            ++failed;
        if (apply(kills_board + 2, uuid, (double)mythics, ms_lb_set, extra))
            ++failed;
    }
    free(extra);
    return failed ? -1 : 0;
}

int ms_lb_push_gardn_best(char const *uuid, uint64_t best_score)
{
    struct ms_db_account a;
    if (best_score == 0 ||
        MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &a) != MS_DB_OK ||
        a.is_dev)
        return 0;
    char *extra = name_json(a.username);
    int failed = apply(MS_LB_PARTERIA_BOARDS + 1, uuid, (double)best_score,
                       ms_lb_max, extra);
    free(extra);
    return failed ? -1 : 0;
}

// ------------------------------------------------------- start up correction

struct verify
{
    int accounts;
    int corrected;
};

extern int ms_server_quitting();

static void verify_run(void *arg)
{
    struct verify *v = arg;
    enum { PAGE = 100 };
    struct ms_db_account_ref *page = calloc(PAGE, sizeof *page);
    for (int offset = 0; !ms_server_quitting(); offset += PAGE)
    {
        int n = MS_DB->account_page(MS_DB, offset, PAGE, page);
        if (n <= 0)
            break;
        for (int i = 0; i < n && !ms_server_quitting(); ++i)
        {
            struct ms_db_account_ref *a = &page[i];
            if (a->is_dev)
                continue;
            ++v->accounts;
            struct ms_db_parteria rys;
            ms_db_parteria_init(&rys);
            if (MS_DB->parteria_get(MS_DB, a->uuid, &rys) == MS_DB_OK)
            {
                // everything a save can prove, without the counters it can not
                if (ms_lb_push_parteria(a->uuid, a->username, &rys, 0, NULL, NULL,
                                        NULL) != 0)
                    ++v->corrected;
            }
            ms_db_parteria_free(&rys);
            // gardn: the best of the saves still there; the player's kills and
            // the rarity of their petals only gardn knows, it reports them
            struct ms_db_gsave *saves = calloc(MS_GARDN_MAX_SAVES, sizeof *saves);
            int count = MS_DB->gardn_list(MS_DB, a->uuid, saves, MS_GARDN_MAX_SAVES);
            if (count > 0)
            {
                uint64_t best = 0;
                for (int s = 0; s < count; ++s)
                    if (saves[s].score > best)
                        best = saves[s].score;
                char *extra = name_json(a->username);
                if (best > 0)
                    apply(MS_LB_PARTERIA_BOARDS + 1, a->uuid, (double)best,
                          ms_lb_max, extra);
                free(extra);
                ms_db_gsave_free(saves, count);
            }
            free(saves);
        }
        if (n < PAGE)
            break;
        // a page at a time with a rest in between: the scan is a background
        // job, the database has players to serve (nothing is kept in memory)
        usleep(50 * 1000);
    }
    free(page);
}

static void verify_done(void *arg)
{
    struct verify *v = arg;
    ms_log("leaderboard", "checked %d accounts%s", v->accounts,
           v->corrected ? ", some boards could not be written" : "");
    free(v);
}

void ms_lb_verify_start()
{
    if (MS_CONFIG.leaderboard_verify == 0 || MS_DB == NULL ||
        MS_DB->account_page == NULL || MS_DB->lb_apply == NULL)
        return;
    ms_log("leaderboard", "checking every account against its saves");
    ms_jobs_submit(verify_run, verify_done, calloc(1, sizeof(struct verify)));
}

// -------------------------------------------------------------------- page

struct top_cache
{
    pthread_mutex_t lock;
    int64_t at_ms;
    int count[sizeof BOARDS / sizeof *BOARDS];
    struct ms_db_lb_entry top[sizeof BOARDS / sizeof *BOARDS][TOP_SIZE];
};
static struct top_cache cache = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void cache_clear()
{
    for (int b = 0; b < BOARD_COUNT; ++b)
        if (cache.count[b] > 0)
            ms_db_lb_free(cache.top[b], cache.count[b]);
    memset(cache.count, 0, sizeof cache.count);
}

// 0 = the lists are there (maybe a little old), -1 = no database
static int cache_refresh()
{
    int64_t now = ms_now_ms();
    if (cache.at_ms != 0 && now - cache.at_ms < CACHE_MS)
        return 0;
    struct ms_db_lb_entry fresh[sizeof BOARDS / sizeof *BOARDS][TOP_SIZE];
    int counts[sizeof BOARDS / sizeof *BOARDS];
    for (int b = 0; b < BOARD_COUNT; ++b)
    {
        memset(fresh[b], 0, sizeof fresh[b]);
        if (strcmp(BOARDS[b].game, "hidden") == 0)
        {
            counts[b] = 0;
            continue;
        }
        counts[b] = MS_DB->lb_top(MS_DB, BOARDS[b].id, TOP_SIZE, fresh[b]);
        if (counts[b] < 0)
        {
            for (int c = 0; c < b; ++c)
                if (counts[c] > 0)
                    ms_db_lb_free(fresh[c], counts[c]);
            return cache.at_ms != 0 ? 0 : -1; // keep serving the old lists
        }
    }
    cache_clear();
    memcpy(cache.top, fresh, sizeof cache.top);
    memcpy(cache.count, counts, sizeof cache.count);
    cache.at_ms = now;
    return 0;
}

static char const *name_of(char const *extra_json, char *out, size_t size)
{
    cJSON *e = extra_json ? cJSON_Parse(extra_json) : NULL;
    char const *name = ms_json_str(e, "name");
    ms_strlcpy(out, name ? name : "?", size);
    cJSON_Delete(e);
    return out;
}

// GET /leaderboard?game=rysteria|gardn&me=<uuid>
// Every board of the game (both games without `game`), the best ten of each and
// (when `me` is given) where that player stands. uuids of others are never sent.
void ms_h_leaderboard(struct ms_req *req, struct ms_res *res)
{
    if (MS_DB == NULL || MS_DB->lb_apply == NULL)
    {
        ms_res_error(res, 503, "unavailable", NULL);
        return;
    }
    char me[64] = "", only[16] = "";
    if (ms_query_arg(req->query, "me", me, sizeof me) && !ms_uuid_valid(me))
        me[0] = 0;
    ms_query_arg(req->query, "game", only, sizeof only);

    cJSON *root = cJSON_CreateObject();
    pthread_mutex_lock(&cache.lock);
    if (cache_refresh() != 0)
    {
        pthread_mutex_unlock(&cache.lock);
        cJSON_Delete(root);
        ms_res_error(res, 503, "unavailable", NULL);
        return;
    }
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddNumberToObject(root, "generated", (double)(cache.at_ms / 1000));
    cJSON *games = cJSON_AddArrayToObject(root, "games");
    for (size_t g = 0; g < sizeof GAME_IDS / sizeof *GAME_IDS; ++g)
    {
        if (only[0] && strcmp(only, GAME_IDS[g]) != 0)
            continue;
        cJSON *game = cJSON_CreateObject();
        cJSON_AddStringToObject(game, "id", GAME_IDS[g]);
        cJSON *boards = cJSON_AddArrayToObject(game, "boards");
        cJSON_AddItemToArray(games, game);
        for (int b = 0; b < BOARD_COUNT; ++b)
        {
            if (strcmp(BOARDS[b].game, GAME_IDS[g]) != 0)
                continue;
            cJSON *board = cJSON_CreateObject();
            cJSON_AddStringToObject(board, "id", BOARDS[b].id);
            cJSON_AddStringToObject(board, "title", BOARDS[b].title);
            cJSON_AddStringToObject(board, "note", BOARDS[b].note);
            cJSON_AddStringToObject(board, "format", BOARDS[b].format);
            cJSON *entries = cJSON_AddArrayToObject(board, "entries");
            for (int i = 0; i < cache.count[b]; ++i)
            {
                struct ms_db_lb_entry const *e = &cache.top[b][i];
                char name[65];
                cJSON *row = cJSON_CreateObject();
                cJSON_AddNumberToObject(row, "rank", i + 1);
                cJSON_AddStringToObject(row, "name",
                                        name_of(e->extra, name, sizeof name));
                cJSON_AddNumberToObject(row, "score", e->score);
                if (me[0] && strcmp(e->uuid, me) == 0)
                    cJSON_AddBoolToObject(row, "me", 1);
                cJSON_AddItemToArray(entries, row);
            }
            cJSON_AddItemToArray(boards, board);
        }
    }
    pthread_mutex_unlock(&cache.lock);

    // where the player stands, also when they are not in the top ten
    if (me[0])
    {
        cJSON *game_it;
        cJSON_ArrayForEach(game_it, games)
        {
            cJSON *board;
            cJSON_ArrayForEach(board, cJSON_GetObjectItem(game_it, "boards"))
            {
                char const *id = ms_json_str(board, "id");
                double score = 0;
                int64_t rank = 0;
                if (id != NULL &&
                    MS_DB->lb_rank(MS_DB, id, me, &score, &rank) == MS_DB_OK)
                {
                    cJSON *mine = cJSON_AddObjectToObject(board, "me");
                    cJSON_AddNumberToObject(mine, "rank", (double)rank);
                    cJSON_AddNumberToObject(mine, "score", score);
                }
            }
        }
    }
    ms_res_json(res, 200, root);
}

// ----------------------------------------------------------------- profiles

static double board_score(int board, char const *uuid, int64_t *rank)
{
    double score = 0;
    int64_t r = 0;
    if (MS_DB->lb_rank(MS_DB, BOARDS[board].id, uuid, &score, &r) != MS_DB_OK)
        score = r = 0;
    if (rank != NULL)
        *rank = r;
    return score;
}

// GET /profile?uuid=<uuid> or ?name=<username>
// What anybody may see of an account: name, level, the numbers of the
// leaderboards with the place on each, the loadout and the minigames record.
// No email, no links, no tokens. The stats page links to it by uuid.
void ms_h_profile(struct ms_req *req, struct ms_res *res)
{
    char uuid[64], name[80];
    int have_uuid = ms_query_arg(req->query, "uuid", uuid, sizeof uuid) && uuid[0];
    int have_name = ms_query_arg(req->query, "name", name, sizeof name) && name[0];
    struct ms_db_account a;
    int rc;
    if (have_uuid && ms_uuid_valid(uuid))
        rc = MS_DB->account_get(MS_DB, ms_db_by_uuid, uuid, NULL, &a);
    else if (have_name && !have_uuid)
    {
        char lower[80];
        ms_str_lower(lower, name, sizeof lower);
        rc = MS_DB->account_get(MS_DB, ms_db_by_username, lower, NULL, &a);
    }
    else
    {
        ms_res_error(res, 400, "bad_request", NULL);
        return;
    }
    if (rc == MS_DB_ERROR)
    {
        ms_res_error(res, 500, "server_error", NULL);
        return;
    }
    if (rc != MS_DB_OK || a.is_dev)
    {
        ms_res_error(res, 404, "not_found", NULL);
        return;
    }

    struct ms_db_parteria rys;
    ms_db_parteria_init(&rys);
    int have_rys = MS_DB->parteria_get(MS_DB, a.uuid, &rys) == MS_DB_OK;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddStringToObject(root, "uuid", a.uuid);
    cJSON_AddStringToObject(root, "name", a.username);
    cJSON_AddNumberToObject(root, "created", (double)a.created_at);
    cJSON_AddNumberToObject(root, "level",
                            have_rys ? floor(level_score(rys.xp)) : 1);
    if (have_rys && rys.nickname[0])
        cJSON_AddStringToObject(root, "nickname", rys.nickname);

    // the Parteria boards, in the order of the leaderboard page
    cJSON *stats = cJSON_AddArrayToObject(root, "stats");
    for (int b = 0; b < MS_LB_PARTERIA_BOARDS; ++b)
    {
        int64_t rank;
        double score = board_score(b, a.uuid, &rank);
        cJSON *row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "id", BOARDS[b].id);
        cJSON_AddStringToObject(row, "title", BOARDS[b].title);
        cJSON_AddStringToObject(row, "format", BOARDS[b].format);
        cJSON_AddNumberToObject(row, "score", score);
        if (rank > 0)
            cJSON_AddNumberToObject(row, "rank", (double)rank);
        cJSON_AddItemToArray(stats, row);
    }
    cJSON_AddNumberToObject(root, "pvp_best",
                            board_score(2, a.uuid, NULL));

    // the minigames: won, played, lost and the percentage of wins
    cJSON *mg = cJSON_AddObjectToObject(root, "minigames");
    for (int m = 0; m < 3; ++m)
    {
        int64_t rank;
        double wins = board_score(MS_LB_MG_WINS + m, a.uuid, &rank);
        double played = board_score(MS_LB_MG_PLAYED + m, a.uuid, NULL);
        if (played < wins)
            played = wins;
        cJSON *o = cJSON_AddObjectToObject(mg, MG_MODES[m]);
        cJSON_AddNumberToObject(o, "wins", wins);
        cJSON_AddNumberToObject(o, "played", played);
        cJSON_AddNumberToObject(o, "lost", played - wins);
        cJSON_AddNumberToObject(o, "win_percent",
                                played > 0 ? floor(1000 * wins / played) / 10 : 0);
        if (rank > 0)
            cJSON_AddNumberToObject(o, "rank", (double)rank);
    }

    // the loadout as the account holds it: {"slot": [petal id, rarity]}
    cJSON *loadout = have_rys && rys.loadout ? cJSON_Parse(rys.loadout) : NULL;
    cJSON_AddItemToObject(root, "loadout",
                          loadout != NULL ? loadout : cJSON_CreateObject());
    ms_db_parteria_free(&rys);
    ms_res_json(res, 200, root);
}

// GET /profile/search?q=<start of a username>
// Up to ten usernames that start with it, to find somebody's stats page.
void ms_h_profile_search(struct ms_req *req, struct ms_res *res)
{
    char q[80], lower[80], clean[80];
    if (!ms_query_arg(req->query, "q", q, sizeof q) || q[0] == 0 ||
        strlen(q) > 64)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        return;
    }
    ms_str_lower(lower, q, sizeof lower);
    // usernames hold letters, digits and _ . - and spaces only
    size_t o = 0;
    for (char const *c = lower; *c; ++c)
        if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' ||
            *c == '.' || *c == '-' || *c == ' ')
            clean[o++] = *c;
    clean[o] = 0;
    if (o == 0 || MS_DB->account_search == NULL)
    {
        ms_res_error(res, 400, "bad_request", NULL);
        return;
    }
    struct ms_db_account_ref found[10];
    int n = MS_DB->account_search(MS_DB, clean, 10, found);
    if (n < 0)
    {
        ms_res_error(res, 500, "server_error", NULL);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON *list = cJSON_AddArrayToObject(root, "results");
    for (int i = 0; i < n; ++i)
    {
        cJSON *row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "uuid", found[i].uuid);
        cJSON_AddStringToObject(row, "name", found[i].username);
        cJSON_AddItemToArray(list, row);
    }
    ms_res_json(res, 200, root);
}

// ---------------------------------------------------------- minigame results

// The minigames server sends the end of a match (games.c, op 9). It only counts
// when the match was started with the PIN (MINIGAMES_PIN of the master); a wrong
// or missing PIN changes nothing. Any thread (it reads the database).
int ms_lb_minigame_result(char const *mode, char const *pin, int count,
                          char const (*uuids)[MS_UUID_SIZE],
                          uint8_t const *won)
{
    int m = -1;
    for (int i = 0; i < 3; ++i)
        if (strcmp(mode, MG_MODES[i]) == 0)
            m = i;
    if (m < 0 || MS_CONFIG.minigames_pin[0] == 0 ||
        !ms_ct_str_equal(pin, MS_CONFIG.minigames_pin))
        return -1;
    int failed = 0;
    for (int i = 0; i < count; ++i)
    {
        struct ms_db_account a;
        if (!ms_uuid_valid(uuids[i]) ||
            MS_DB->account_get(MS_DB, ms_db_by_uuid, uuids[i], NULL, &a) !=
                MS_DB_OK ||
            a.is_dev)
            continue;
        char *extra = name_json(a.username);
        if (apply(MS_LB_MG_PLAYED + m, a.uuid, 1, ms_lb_add, extra) != 0)
            ++failed;
        if (won[i] && apply(MS_LB_MG_WINS + m, a.uuid, 1, ms_lb_add, extra) != 0)
            ++failed;
        free(extra);
    }
    return failed ? -1 : 0;
}

// ------------------------------------------------------------- gardn stats

// every petal in every gardn save is counted by reading all the saves, so it is
// done at most this often, whatever the number of visitors
#define STATS_CACHE_MS (10 * 60 * 1000)
#define STATS_MAX_PETALS 512

struct stats_cache
{
    pthread_mutex_t lock;
    int64_t at_ms;
    int count;
    int64_t saves;
    int64_t players;
    struct ms_db_petal_stat petals[STATS_MAX_PETALS];
};
static struct stats_cache stats = {.lock = PTHREAD_MUTEX_INITIALIZER};

// GET /stats/gardn
// How often each petal is in the saves of gardn: `copies` slots, `saves` saves
// holding it, most copies first. The names of the petals are the page's.
void ms_h_gardn_stats(struct ms_req *req, struct ms_res *res)
{
    (void)req;
    if (MS_DB == NULL || MS_DB->gardn_petal_stats == NULL)
    {
        ms_res_error(res, 503, "unavailable", NULL);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    pthread_mutex_lock(&stats.lock);
    int64_t now = ms_now_ms();
    if (stats.at_ms == 0 || now - stats.at_ms >= STATS_CACHE_MS)
    {
        int64_t saves = 0, players = 0;
        static struct ms_db_petal_stat fresh[STATS_MAX_PETALS];
        int n = MS_DB->gardn_petal_stats(MS_DB, fresh, STATS_MAX_PETALS, &saves,
                                         &players);
        if (n >= 0)
        {
            memcpy(stats.petals, fresh, sizeof(fresh[0]) * (size_t)n);
            stats.count = n;
            stats.saves = saves;
            stats.players = players;
            stats.at_ms = now;
        }
        else if (stats.at_ms == 0)
        {
            pthread_mutex_unlock(&stats.lock);
            cJSON_Delete(root);
            ms_res_error(res, 503, "unavailable", NULL);
            return;
        } // else: the old numbers are still good
    }
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddNumberToObject(root, "generated", (double)(stats.at_ms / 1000));
    cJSON_AddNumberToObject(root, "saves", (double)stats.saves);
    cJSON_AddNumberToObject(root, "players", (double)stats.players);
    cJSON *petals = cJSON_AddArrayToObject(root, "petals");
    for (int i = 0; i < stats.count; ++i)
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "id", stats.petals[i].id);
        cJSON_AddNumberToObject(p, "copies", (double)stats.petals[i].copies);
        cJSON_AddNumberToObject(p, "saves", (double)stats.petals[i].saves);
        cJSON_AddItemToArray(petals, p);
    }
    pthread_mutex_unlock(&stats.lock);
    ms_res_json(res, 200, root);
}
