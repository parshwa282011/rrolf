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

#include "games.h"

#include <cJSON.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "api.h"
#include "config.h"
#include "jobs.h"
#include "leaderboard.h"
#include "proto.h"
#include "util.h"

#define MAX_RX (64 * 1024)
#define JOIN_WAIT_SECONDS 3
#define SLOTS 64
// squads a game server reports, and how many all of them together are sent on
#define DIR_PER_SERVER 64
#define DIR_TOTAL 120
// how often the squad directory is sent even when nothing changed, for servers
// that connected in between
#define DIR_REFRESH_MS 10000
#define RESTART_JOBS 8

struct outmsg
{
    struct outmsg *next;
    size_t len;
    uint8_t *buf; // LWS_PRE bytes of padding, then the payload
};

// one squad of a game server, as it reports them (op 7)
struct dir_entry
{
    char code[16];   // already prefixed, or "(private)"
    char leader[17];
    uint8_t members;
    uint8_t is_private;
};

// per game server connection (the websocket user data)
struct ms_gconn
{
    struct ms_gconn *next;
    uint64_t id;
    struct lws *wsi;
    char alias[8];
    char name[32]; // what the server calls itself (op 101), "" until it says
    int intentional; // it said it is shutting down (op 8): do not restart it
    struct dir_entry dir[DIR_PER_SERVER];
    uint8_t dir_count;
    uint64_t dir_sent; // dir_version this server was last sent
    int64_t dir_sent_ms;
    char uuids[SLOTS][MS_UUID_SIZE]; // by slot, "" = empty
    struct outmsg *out_head, *out_tail;
    uint8_t *rx;
    size_t rx_len;
    int closing;
};

// An online (or just-left, still unsaved) player. The cache copy is
// authoritative while it exists.
struct player
{
    struct player *next;
    char uuid[MS_UUID_SIZE];
    struct ms_gconn *conn; // NULL once the player left
    uint8_t pos;
    char username[65];
    int is_dev;
    double xp;
    int checkpoint; // see struct ms_db_rysteria
    int64_t pvp_points;
    uint64_t nonce; // picked by the game server, echoed so it can match replies
    char token[MS_PL_P_SIZE];
    cJSON *petals;
    cJSON *failed_crafts;
    cJSON *mob_kills;
    cJSON *loadout;
    char nickname[MS_NICKNAME_SIZE];
    uint64_t version;       // bumped on every change
    uint64_t flushed_version;
    int flushing;
    int64_t last_flush_ms;
    // leaderboards (leaderboard.c): what is on the boards for this player (-1 =
    // not known yet), the highest PvP score the game server saw, and the Ancient
    // craft attempts and PvP kills it has told us about that no board has yet
    double lb_last[MS_LB_RYSTERIA_BOARDS];
    int64_t pvp_best;
    struct ms_lb_counters lb_count;
};

struct join
{
    struct join *next;
    uint64_t conn_id;
    uint8_t pos;
    uint64_t nonce;
    char uuid[MS_UUID_SIZE];
    char pl_p[MS_PL_P_SIZE];
    // filled by the worker
    enum ms_auth_result verdict;
    struct ms_db_account account;
    struct ms_db_rysteria rys;
    int64_t deadline;
};

struct flush
{
    char uuid[MS_UUID_SIZE];
    char username[65];
    uint64_t version;
    struct ms_db_rysteria rys;
    int rc;
    int is_dev;
    // leaderboards
    double lb_prev[MS_LB_RYSTERIA_BOARDS];
    double lb_now[MS_LB_RYSTERIA_BOARDS];
    int64_t pvp_best;
    struct ms_lb_counters count;
    int lb_rc;
};

static struct lws_context *context;
static struct ms_gconn *conns;
static struct player *players;
static struct join *waiting;
static uint64_t next_conn_id = 1;
static unsigned next_alias = 1296; // "100" in base 36, like the old master
static int64_t last_count_log;
// bumped whenever a squad list changes or a game server comes or goes
static uint64_t dir_version = 1;

// A game server that dropped its connection is started again, once. The job
// lives until the server reconnects, the command fails or the time is up.
struct restart_job
{
    int used;
    char name[32];
    pid_t pid; // 0 once the command has exited
    int64_t started_ms;
};
static struct restart_job restart_jobs[RESTART_JOBS];

// kicks requested from worker threads
struct kick
{
    struct kick *next;
    char uuid[MS_UUID_SIZE];
};
static pthread_mutex_t kick_lock = PTHREAD_MUTEX_INITIALIZER;
static struct kick *kick_head;

void ms_games_set_context(struct lws_context *c) { context = c; }
void ms_games_init() {}
size_t ms_games_session_size() { return sizeof(struct ms_gconn); }

// ---------------------------------------------------------------- helpers

static struct player *find_player(char const *uuid)
{
    for (struct player *p = players; p; p = p->next)
        if (strcmp(p->uuid, uuid) == 0)
            return p;
    return NULL;
}

static struct ms_gconn *find_conn(uint64_t id)
{
    for (struct ms_gconn *c = conns; c; c = c->next)
        if (c->id == id && !c->closing)
            return c;
    return NULL;
}

static void free_player(struct player *p)
{
    struct player **link = &players;
    while (*link && *link != p)
        link = &(*link)->next;
    if (*link)
        *link = p->next;
    cJSON_Delete(p->petals);
    cJSON_Delete(p->failed_crafts);
    cJSON_Delete(p->mob_kills);
    cJSON_Delete(p->loadout);
    free(p);
}

static cJSON *parse_object(char const *text)
{
    cJSON *o = text ? cJSON_Parse(text) : NULL;
    if (o == NULL || !cJSON_IsObject(o))
    {
        cJSON_Delete(o);
        o = cJSON_CreateObject();
    }
    return o;
}

static void queue_message(struct ms_gconn *c, struct ms_writer *w)
{
    if (c->closing || w->error)
    {
        ms_writer_free(w);
        return;
    }
    struct outmsg *m = malloc(sizeof *m);
    m->len = w->at;
    m->buf = malloc(LWS_PRE + w->at);
    memcpy(m->buf + LWS_PRE, w->data, w->at);
    m->next = NULL;
    if (c->out_tail)
        c->out_tail->next = m;
    else
        c->out_head = m;
    c->out_tail = m;
    ms_writer_free(w);
    lws_callback_on_writable(c->wsi);
}

// [45][2][pos][nonce]: tells the game server to drop that slot (the nonce is
// the one the game server sent with the join, so it only hits that connection)
static void send_kick(struct ms_gconn *c, uint8_t pos, uint64_t nonce)
{
    struct ms_writer w;
    ms_writer_init(&w, 64, 1);
    ms_writer_u8(&w, 2);
    ms_writer_u8(&w, pos);
    ms_writer_varuint(&w, nonce);
    queue_message(c, &w);
}

static void write_counts(struct ms_writer *w, cJSON const *map)
{
    cJSON const *item;
    cJSON_ArrayForEach(item, map)
    {
        unsigned id, rarity;
        if (!cJSON_IsNumber(item) || sscanf(item->string, "%u:%u", &id, &rarity) != 2 ||
            id < 1 || id > 255 || rarity > 255 || !(item->valuedouble >= 1))
            continue;
        double count = item->valuedouble;
        if (count > 4294967295.0)
            count = 4294967295.0;
        ms_writer_u8(w, (uint8_t)id);
        ms_writer_u8(w, (uint8_t)rarity);
        ms_writer_varuint(w, (uint64_t)count);
    }
    ms_writer_u8(w, 0);
}

static void write_loadout(struct ms_writer *w, cJSON const *map)
{
    cJSON const *item;
    cJSON_ArrayForEach(item, map)
    {
        unsigned slot = (unsigned)atoi(item->string);
        cJSON const *id = cJSON_GetArrayItem(item, 0);
        cJSON const *rarity = cJSON_GetArrayItem(item, 1);
        if (!cJSON_IsArray(item) || !cJSON_IsNumber(id) || !cJSON_IsNumber(rarity) ||
            slot < 1 || slot > 255 || id->valuedouble < 0 || id->valuedouble > 255 ||
            rarity->valuedouble < 0 || rarity->valuedouble > 255)
            continue;
        ms_writer_u8(w, (uint8_t)slot);
        ms_writer_u8(w, (uint8_t)id->valuedouble);
        ms_writer_u8(w, (uint8_t)rarity->valuedouble);
    }
    ms_writer_u8(w, 0);
}

static void send_join_ok(struct ms_gconn *c, struct player *p)
{
    struct ms_writer w;
    ms_writer_init(&w, 64 * 1024, 1);
    ms_writer_u8(&w, 1);
    ms_writer_u8(&w, p->pos);
    ms_writer_varuint(&w, p->nonce);
    ms_writer_string(&w, p->uuid);
    ms_writer_string(&w, p->token); // the game server hands it back to the client
    ms_writer_string(&w, p->username);
    ms_writer_string(&w, ""); // id: discord oauth2 id, not used
    ms_writer_string(&w, ""); // cg_id: CrazyGames id, not used
    ms_writer_u8(&w, (uint8_t)p->is_dev);
    ms_writer_f64(&w, p->xp);
    ms_writer_varuint(&w, p->pvp_points > 0 ? (uint64_t)p->pvp_points : 0);
    ms_writer_u8(&w, (uint8_t)(p->checkpoint & 0xff));
    ms_writer_u8(&w, (uint8_t)((p->checkpoint >> 8) & 0xff));
    ms_writer_u8(&w, (uint8_t)((p->checkpoint >> 16) & 0xff));
    write_counts(&w, p->petals);
    write_counts(&w, p->failed_crafts);
    write_counts(&w, p->mob_kills);
    write_loadout(&w, p->loadout);
    ms_writer_string(&w, p->nickname);
    queue_message(c, &w);
}

// --------------------------------------------------------------- flushing

static void flush_run(void *arg)
{
    struct flush *f = arg;
    f->rc = MS_DB->rysteria_put(MS_DB, f->uuid, &f->rys);
    // the leaderboards follow what was just saved
    memcpy(f->lb_now, f->lb_prev, sizeof f->lb_now);
    f->lb_rc = 0;
    if (f->rc == MS_DB_OK && !f->is_dev)
        f->lb_rc = ms_lb_push_rysteria(f->uuid, f->username, &f->rys,
                                       f->pvp_best, &f->count, f->lb_prev,
                                       f->lb_now);
}

static void flush_done(void *arg)
{
    struct flush *f = arg;
    struct player *p = find_player(f->uuid);
    if (p != NULL)
    {
        p->flushing = 0;
        memcpy(p->lb_last, f->lb_now, sizeof p->lb_last);
        // counts no board got (the save or the board failed) are told again
        if (f->rc != MS_DB_OK || f->lb_rc != 0)
        {
            p->lb_count.attempts += f->count.attempts;
            p->lb_count.pvp_kills += f->count.pvp_kills;
        }
        if (f->rc == MS_DB_OK)
        {
            if (f->version > p->flushed_version)
                p->flushed_version = f->version;
            // a player who already left and has nothing newer is done
            if (p->conn == NULL && p->flushed_version == p->version)
                free_player(p);
        }
        else
            ms_log("games", "save failed for %s, will retry", f->uuid);
    }
    ms_db_rysteria_free(&f->rys);
    free(f);
}

static void snapshot(struct player const *p, struct ms_db_rysteria *r)
{
    memset(r, 0, sizeof *r);
    r->xp = p->xp;
    r->checkpoint = p->checkpoint;
    r->pvp_points = p->pvp_points;
    r->petals = cJSON_PrintUnformatted(p->petals);
    r->failed_crafts = cJSON_PrintUnformatted(p->failed_crafts);
    r->mob_kills = cJSON_PrintUnformatted(p->mob_kills);
    r->loadout = cJSON_PrintUnformatted(p->loadout);
    ms_strlcpy(r->nickname, p->nickname, sizeof r->nickname);
}

static void start_flush(struct player *p)
{
    if (p->flushing || p->version == p->flushed_version)
        return;
    struct flush *f = calloc(1, sizeof *f);
    ms_strlcpy(f->uuid, p->uuid, sizeof f->uuid);
    ms_strlcpy(f->username, p->username, sizeof f->username);
    f->version = p->version;
    f->is_dev = p->is_dev;
    memcpy(f->lb_prev, p->lb_last, sizeof f->lb_prev);
    f->pvp_best = p->pvp_best;
    f->count = p->lb_count;
    memset(&p->lb_count, 0, sizeof p->lb_count);
    snapshot(p, &f->rys);
    p->flushing = 1;
    p->last_flush_ms = ms_now_ms();
    ms_jobs_submit(flush_run, flush_done, f);
}

// ------------------------------------------------------------------ joins

static void adopt(struct join *j, struct ms_gconn *c)
{
    struct player *p = find_player(j->uuid);
    if (p == NULL)
    {
        p = calloc(1, sizeof *p);
        ms_strlcpy(p->uuid, j->uuid, sizeof p->uuid);
        p->xp = j->rys.xp;
        p->checkpoint = j->rys.checkpoint;
        p->pvp_points = j->rys.pvp_points;
        p->pvp_best = j->rys.pvp_points;
        for (int b = 0; b < MS_LB_RYSTERIA_BOARDS; ++b)
            p->lb_last[b] = -1;
        p->petals = parse_object(j->rys.petals);
        p->failed_crafts = parse_object(j->rys.failed_crafts);
        p->mob_kills = parse_object(j->rys.mob_kills);
        p->loadout = parse_object(j->rys.loadout);
        ms_strlcpy(p->nickname, j->rys.nickname, sizeof p->nickname);
        // a brand new record counts as unsaved so it gets written
        p->version = 1;
        p->flushed_version = j->rys.nickname[0] || j->rys.xp > 0 ? 1 : 0;
        p->next = players;
        players = p;
    }
    else if (p->conn != NULL)
    {
        // forced takeover of a session that never answered the kick
        p->conn->uuids[p->pos][0] = 0;
        p->conn = NULL;
    }
    ms_strlcpy(p->username, j->account.username, sizeof p->username);
    ms_strlcpy(p->token, j->pl_p, sizeof p->token);
    p->nonce = j->nonce;
    p->is_dev = j->account.is_dev;
    p->conn = c;
    p->pos = j->pos;
    ms_strlcpy(c->uuids[j->pos], j->uuid, MS_UUID_SIZE);
    ms_log("games", "join %s on %s slot %u", j->uuid, c->alias, j->pos);
    send_join_ok(c, p);
}

static void join_free(struct join *j)
{
    ms_db_rysteria_free(&j->rys);
    free(j);
}

static void join_run(void *arg)
{
    struct join *j = arg;
    ms_db_rysteria_init(&j->rys);
    j->verdict = ms_auth_verify(j->uuid, NULL, j->pl_p, &j->account);
    if (j->verdict != ms_auth_ok)
        return;
    int rc = MS_DB->rysteria_get(MS_DB, j->uuid, &j->rys);
    if (rc == MS_DB_NOT_FOUND)
    {
        // new account: same starting inventory the old master gave
        free(j->rys.petals);
        j->rys.petals = strdup("{\"1:0\":5}");
    }
    else if (rc != MS_DB_OK)
        j->verdict = ms_auth_error;
    // the player is online now: their numbers on the leaderboards are put right
    else if (!j->account.is_dev)
        ms_lb_push_rysteria(j->uuid, j->account.username, &j->rys, 0, NULL, NULL,
                            NULL);
}

static void join_done(void *arg)
{
    struct join *j = arg;
    struct ms_gconn *c = find_conn(j->conn_id);
    if (c == NULL)
    {
        join_free(j);
        return;
    }
    if (j->verdict != ms_auth_ok)
    {
        ms_log("games", "join denied for %s (%s)", j->uuid,
               j->verdict == ms_auth_expired ? "expired" : "invalid");
        send_kick(c, j->pos, j->nonce);
        join_free(j);
        return;
    }
    struct player *existing = find_player(j->uuid);
    if (existing != NULL && existing->conn != NULL)
    {
        // same account somewhere else: kick the old session, then wait for its
        // final save + leave before taking over
        send_kick(existing->conn, existing->pos, existing->nonce);
        j->deadline = ms_now() + JOIN_WAIT_SECONDS;
        j->next = waiting;
        waiting = j;
        return;
    }
    adopt(j, c);
    join_free(j);
}

static void resolve_waiting()
{
    struct join **link = &waiting;
    int64_t now = ms_now();
    while (*link)
    {
        struct join *j = *link;
        struct player *p = find_player(j->uuid);
        if (p == NULL || p->conn == NULL || now >= j->deadline)
        {
            *link = j->next;
            struct ms_gconn *c = find_conn(j->conn_id);
            if (c != NULL)
                adopt(j, c);
            join_free(j);
        }
        else
            link = &j->next;
    }
}

static void player_left(struct player *p)
{
    if (p->conn != NULL)
        p->conn->uuids[p->pos][0] = 0;
    p->conn = NULL;
    if (p->version == p->flushed_version && !p->flushing)
        free_player(p);
    else
        start_flush(p);
    resolve_waiting();
}

// --------------------------------------------------------------- messages

static void read_counts(struct ms_reader *r, cJSON *out)
{
    uint8_t id;
    while ((id = ms_reader_u8(r)) != 0 && !r->error)
    {
        uint8_t rarity = ms_reader_u8(r);
        uint64_t count = ms_reader_varuint(r);
        if (r->error)
            return;
        char key[16];
        snprintf(key, sizeof key, "%u:%u", id, rarity);
        cJSON_DeleteItemFromObjectCaseSensitive(out, key);
        cJSON_AddNumberToObject(out, key, (double)count);
    }
}

static void read_loadout(struct ms_reader *r, cJSON *out)
{
    uint8_t slot;
    while ((slot = ms_reader_u8(r)) != 0 && !r->error)
    {
        uint8_t id = ms_reader_u8(r);
        uint8_t rarity = ms_reader_u8(r);
        if (r->error)
            return;
        char key[8];
        snprintf(key, sizeof key, "%u", slot);
        cJSON *pair = cJSON_CreateArray();
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(id));
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(rarity));
        cJSON_AddItemToObject(out, key, pair);
    }
}

static void on_join(struct ms_gconn *c, struct ms_reader *r)
{
    struct join *j = calloc(1, sizeof *j);
    char uuid[128];
    char code[256]; // the old discord oauth2 code, not used
    ms_reader_string(r, uuid, sizeof uuid);
    ms_reader_string(r, j->pl_p, sizeof j->pl_p);
    ms_reader_string(r, code, sizeof code);
    j->nonce = ms_reader_varuint(r);
    j->pos = ms_reader_u8(r);
    j->conn_id = c->id;
    if (r->error || j->pos >= SLOTS || !ms_uuid_valid(uuid))
    {
        // answer anyway so the game server frees the slot
        send_kick(c, j->pos < SLOTS ? j->pos : 0, j->nonce);
        free(j);
        return;
    }
    ms_strlcpy(j->uuid, uuid, sizeof j->uuid);
    ms_jobs_submit(join_run, join_done, j);
}

static void on_leave(struct ms_gconn *c, struct ms_reader *r)
{
    char uuid[128];
    ms_reader_string(r, uuid, sizeof uuid);
    if (r->error)
        return;
    struct player *p = find_player(uuid);
    if (p != NULL && p->conn == c)
    {
        ms_log("games", "leave %s", uuid);
        player_left(p);
    }
}

static void on_save(struct ms_gconn *c, struct ms_reader *r)
{
    char uuid[128];
    ms_reader_string(r, uuid, sizeof uuid);
    double xp = ms_reader_f64(r);
    uint64_t pvp_points = ms_reader_varuint(r);
    int checkpoint = ms_reader_u8(r);
    checkpoint |= ms_reader_u8(r) << 8;
    checkpoint |= ms_reader_u8(r) << 16;
    cJSON *petals = cJSON_CreateObject();
    cJSON *failed = cJSON_CreateObject();
    cJSON *mobs = cJSON_CreateObject();
    cJSON *loadout = NULL;
    char nickname[64] = "";
    int has_tail = 0;
    read_counts(r, petals);
    read_counts(r, failed);
    read_counts(r, mobs);
    // loadout and nickname are a tail an older game server does not send
    if (!r->error && ms_reader_remaining(r) > 0)
    {
        has_tail = 1;
        loadout = cJSON_CreateObject();
        read_loadout(r, loadout);
        ms_reader_string(r, nickname, sizeof nickname);
    }
    // and after that what the leaderboards need: Ancient craft attempts since the
    // last save, the highest PvP score and the players killed in PvP since the
    // last save (a server that is older than this one stops after the score)
    uint64_t attempts = 0, pvp_best = 0, pvp_kills = 0;
    if (has_tail && !r->error && ms_reader_remaining(r) > 0)
    {
        attempts = ms_reader_varuint(r);
        pvp_best = ms_reader_varuint(r);
        if (!r->error && ms_reader_remaining(r) > 0)
            pvp_kills = ms_reader_varuint(r);
        if (r->error)
            attempts = pvp_best = pvp_kills = 0;
    }
    struct player *p = r->error ? NULL : find_player(uuid);
    if (p == NULL || p->conn != c || !isfinite(xp) || xp < 0)
    {
        cJSON_Delete(petals);
        cJSON_Delete(failed);
        cJSON_Delete(mobs);
        cJSON_Delete(loadout);
        return;
    }
    p->xp = xp;
    p->pvp_points = pvp_points > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)pvp_points;
    p->checkpoint = checkpoint;
    cJSON_Delete(p->petals);
    p->petals = petals;
    cJSON_Delete(p->failed_crafts);
    p->failed_crafts = failed;
    cJSON_Delete(p->mob_kills);
    p->mob_kills = mobs;
    if (has_tail)
    {
        cJSON_Delete(p->loadout);
        p->loadout = loadout;
        ms_strlcpy(p->nickname, nickname, sizeof p->nickname);
    }
    if (attempts > 0 && attempts < 1000000)
        p->lb_count.attempts += attempts;
    if (pvp_kills > 0 && pvp_kills < 1000000)
        p->lb_count.pvp_kills += pvp_kills;
    if (pvp_best > (uint64_t)p->pvp_best && pvp_best < (uint64_t)INT64_MAX)
        p->pvp_best = (int64_t)pvp_best;
    ++p->version;
}

// op 5: the game's own account requests (linking Discord / CrazyGames, transfer
// codes). The master has a login screen for that instead, so say so and let
// the game server show the message. [45][3][pos][nonce][ok][kind] message\0
// uuid\0 token\0 code\0
static void on_account_request(struct ms_gconn *c, struct ms_reader *r)
{
    uint8_t pos = ms_reader_u8(r);
    uint64_t nonce = ms_reader_varuint(r);
    uint8_t kind = ms_reader_u8(r);
    if (r->error)
        return;
    struct ms_writer w;
    ms_writer_init(&w, 256, 1);
    ms_writer_u8(&w, 3);
    ms_writer_u8(&w, pos);
    ms_writer_varuint(&w, nonce);
    ms_writer_u8(&w, 0);
    ms_writer_u8(&w, kind);
    ms_writer_string(&w, "Not available: use the login screen.");
    ms_writer_string(&w, "");
    ms_writer_string(&w, "");
    ms_writer_string(&w, "");
    queue_message(c, &w);
}


// ------------------------------------- chat and squads shared between servers

// "rrolf-minigames" is shown as "minigames"
static char const *server_label(struct ms_gconn const *c)
{
    char const *n = c->name[0] ? c->name : c->alias;
    return strncmp(n, "rrolf-", 6) == 0 ? n + 6 : n;
}

// op 6: a chat message a game server accepted from one of its players. Every
// other game server shows it to its players too. [45][4] server\0 name\0
// message\0. The message is not stored.
static void on_chat(struct ms_gconn *from, struct ms_reader *r)
{
    char name[17], message[65];
    ms_reader_string(r, name, sizeof name);
    ms_reader_string(r, message, sizeof message);
    if (r->error || name[0] == 0 || message[0] == 0)
        return;
    for (struct ms_gconn *c = conns; c; c = c->next)
    {
        if (c == from || c->closing)
            continue;
        struct ms_writer w;
        ms_writer_init(&w, 160, 1);
        ms_writer_u8(&w, 4);
        ms_writer_string(&w, server_label(from));
        ms_writer_string(&w, name);
        ms_writer_string(&w, message);
        queue_message(c, &w);
    }
}

// op 7: the squads on a game server, sent every couple of seconds:
// count:u8 then per squad code\0 members:u8 private:u8 leader\0. The code is
// what the squad shows its players, "(private)" when it hides it.
static void on_squads(struct ms_gconn *c, struct ms_reader *r)
{
    struct dir_entry list[DIR_PER_SERVER];
    memset(list, 0, sizeof list);
    uint8_t count = ms_reader_u8(r);
    uint8_t got = 0;
    for (uint8_t i = 0; i < count && !r->error; ++i)
    {
        struct dir_entry e;
        memset(&e, 0, sizeof e);
        ms_reader_string(r, e.code, sizeof e.code);
        e.members = ms_reader_u8(r);
        e.is_private = ms_reader_u8(r);
        ms_reader_string(r, e.leader, sizeof e.leader);
        if (!r->error && got < DIR_PER_SERVER)
            list[got++] = e;
    }
    if (r->error)
        return;
    if (got != c->dir_count || memcmp(list, c->dir, sizeof list) != 0)
    {
        memcpy(c->dir, list, sizeof list);
        c->dir_count = got;
        ++dir_version;
    }
}

// op 5 to a game server: the squads of all the OTHER game servers, so its
// players can see every code in one list.
// [45][5] count:u8 then per squad server\0 code\0 members:u8 private:u8 leader\0
static void send_directory(struct ms_gconn *to)
{
    struct ms_writer w;
    ms_writer_init(&w, 16 * 1024, 1);
    ms_writer_u8(&w, 5);
    size_t count_at = w.at;
    ms_writer_u8(&w, 0);
    unsigned count = 0;
    for (struct ms_gconn *c = conns; c; c = c->next)
    {
        if (c == to || c->closing)
            continue;
        for (uint8_t i = 0; i < c->dir_count && count < DIR_TOTAL; ++i)
        {
            struct dir_entry const *e = &c->dir[i];
            ms_writer_string(&w, server_label(c));
            ms_writer_string(&w, e->code);
            ms_writer_u8(&w, e->members);
            ms_writer_u8(&w, e->is_private);
            ms_writer_string(&w, e->leader);
            ++count;
        }
    }
    if (!w.error)
        w.data[count_at] = (uint8_t)count;
    queue_message(to, &w);
    to->dir_sent = dir_version;
    to->dir_sent_ms = ms_now_ms();
}

// ------------------------------------------------ restarting a dropped server

static int safe_server_name(char const *s)
{
    if (s[0] == 0)
        return 0;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
            return 0;
    return 1;
}

// RESTART_COMMAND with {name} replaced by the server's name
static void restart_command_for(char *out, size_t size, char const *name)
{
    char const *in = MS_CONFIG.restart_command;
    size_t o = 0;
    while (*in && o + 1 < size)
    {
        if (strncmp(in, "{name}", 6) == 0)
        {
            for (char const *n = name; *n && o + 1 < size; ++n)
                out[o++] = *n;
            in += 6;
        }
        else
            out[o++] = *in++;
    }
    out[o] = 0;
}

extern char **environ;

// A game server that dropped its connection without saying it was shutting
// down (it crashed, or was killed) is started again, ONCE. If that fails, or it
// does not come back in RESTART_WAIT_SECONDS, nothing more is done for it.
static void maybe_restart(struct ms_gconn const *c)
{
    if (context == NULL || c->intentional || MS_CONFIG.restart_command[0] == 0 ||
        c->name[0] == 0)
        return;
    if (!safe_server_name(c->name))
    {
        ms_log("restart", "not restarting a server with the name \"%s\"",
               c->name);
        return;
    }
    struct restart_job *job = NULL;
    for (int i = 0; i < RESTART_JOBS; ++i)
    {
        if (restart_jobs[i].used && strcmp(restart_jobs[i].name, c->name) == 0)
            return; // already being brought back
        if (!restart_jobs[i].used && job == NULL)
            job = &restart_jobs[i];
    }
    if (job == NULL)
        return;
    char command[1024];
    restart_command_for(command, sizeof command, c->name);
    char *argv[] = {"sh", "-c", command, NULL};
    pid_t pid = 0;
    if (posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) != 0)
    {
        ms_log("restart", "could not run the restart command for %s", c->name);
        return;
    }
    memset(job, 0, sizeof *job);
    job->used = 1;
    ms_strlcpy(job->name, c->name, sizeof job->name);
    job->pid = pid;
    job->started_ms = ms_now_ms();
    ms_log("restart", "game server %s dropped: restarting it once (pid %d)",
           c->name, (int)pid);
}

static void restart_jobs_tick(int64_t now_ms)
{
    for (int i = 0; i < RESTART_JOBS; ++i)
    {
        struct restart_job *job = &restart_jobs[i];
        if (!job->used)
            continue;
        if (job->pid != 0)
        {
            int status = 0;
            pid_t done = waitpid(job->pid, &status, WNOHANG);
            if (done == job->pid)
            {
                job->pid = 0;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                {
                    ms_log("restart", "restarting %s failed (%s %d): giving up",
                           job->name,
                           WIFEXITED(status) ? "exit" : "signal",
                           WIFEXITED(status) ? WEXITSTATUS(status)
                                             : WTERMSIG(status));
                    job->used = 0;
                    continue;
                }
            }
        }
        int back = 0;
        for (struct ms_gconn *c = conns; c; c = c->next)
            if (!c->closing && strcmp(c->name, job->name) == 0)
                back = 1;
        if (back)
        {
            ms_log("restart", "%s is back", job->name);
            job->used = 0;
        }
        else if (now_ms - job->started_ms >=
                 (int64_t)MS_CONFIG.restart_wait_seconds * 1000)
        {
            ms_log("restart", "%s did not come back in %us: giving up",
                   job->name, (unsigned)MS_CONFIG.restart_wait_seconds);
            if (job->pid != 0)
                kill(job->pid, SIGTERM);
            job->used = 0;
        }
    }
}

// op 9: the end of a minigame match. mode\0 pin\0 count:u8, then per player
// uuid\0 won:u8. Counted for the leaderboards only with the right PIN.
struct mg_result
{
    char mode[8];
    char pin[64];
    int count;
    char uuids[64][MS_UUID_SIZE];
    uint8_t won[64];
};

static void mg_result_run(void *arg)
{
    struct mg_result *m = arg;
    if (ms_lb_minigame_result(m->mode, m->pin, m->count,
                              (char const(*)[MS_UUID_SIZE])m->uuids, m->won) ==
        0)
        ms_log("minigames", "%s result counted for %d players", m->mode,
               m->count);
    else
        ms_log("minigames", "%s result not counted (no PIN, wrong PIN or error)",
               m->mode);
}

static void mg_result_done(void *arg) { free(arg); }

static void on_minigame_result(struct ms_reader *r)
{
    struct mg_result *m = calloc(1, sizeof *m);
    if (m == NULL)
        return;
    ms_reader_string(r, m->mode, sizeof m->mode);
    ms_reader_string(r, m->pin, sizeof m->pin);
    uint8_t count = ms_reader_u8(r);
    for (uint8_t i = 0; i < count && i < 64 && !r->error; ++i)
    {
        ms_reader_string(r, m->uuids[i], sizeof m->uuids[i]);
        m->won[i] = ms_reader_u8(r) != 0;
        if (!r->error)
            m->count = i + 1;
    }
    if (r->error || m->count == 0)
    {
        free(m);
        return;
    }
    ms_jobs_submit(mg_result_run, mg_result_done, m);
}

static void handle_message(struct ms_gconn *c, uint8_t const *data, size_t len)
{
    struct ms_reader r;
    ms_reader_init(&r, data, len);
    switch (ms_reader_u8(&r))
    {
    case 0:
        on_join(c, &r);
        break;
    case 1:
        on_leave(c, &r);
        break;
    case 2:
        on_save(c, &r);
        break;
    case 5:
        on_account_request(c, &r);
        break;
    case 6:
        on_chat(c, &r);
        break;
    case 7:
        on_squads(c, &r);
        break;
    case 8: // the server is shutting down on purpose: do not restart it
        c->intentional = 1;
        break;
    case 9:
        on_minigame_result(&r);
        break;
    case 101: // server id: the name the server was started with
    {
        char name[sizeof c->name];
        ms_reader_string(&r, name, sizeof name);
        if (!r.error && name[0] && strcmp(name, c->name) != 0)
        {
            ms_strlcpy(c->name, name, sizeof c->name);
            ++dir_version;
        }
        break;
    }
    case 3: // build log
    case 4: // CrazyGames leaderboard score
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------- websocket

static int authorised(struct lws *wsi)
{
    char path[256] = "";
    lws_hdr_copy(wsi, path, sizeof path, WSI_TOKEN_GET_URI);
    char expected[256];
    snprintf(expected, sizeof expected, "/api/%s", MS_CONFIG.server_secret);
    if (ms_ct_str_equal(path, expected))
        return 1;
    char header[160] = "";
    int n = lws_hdr_custom_copy(wsi, header, sizeof header, "x-server-secret:",
                                16);
    return n > 0 && ms_ct_str_equal(header, MS_CONFIG.server_secret);
}

static void alias_string(char *out, size_t size, unsigned n)
{
    static char const digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[16];
    int i = 0;
    do
    {
        tmp[i++] = digits[n % 36];
        n /= 36;
    } while (n && i < 15);
    size_t o = 0;
    while (i > 0 && o + 1 < size)
        out[o++] = tmp[--i];
    out[o] = 0;
}

static void conn_free(struct ms_gconn *c)
{
    struct ms_gconn **link = &conns;
    while (*link && *link != c)
        link = &(*link)->next;
    if (*link)
        *link = c->next;
    while (c->out_head)
    {
        struct outmsg *m = c->out_head;
        c->out_head = m->next;
        free(m->buf);
        free(m);
    }
    free(c->rx);
}

int ms_games_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    struct ms_gconn *c = user;
    switch (reason)
    {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
        if (!authorised(wsi))
        {
            ms_log("games", "rejected game server connection");
            return 1;
        }
        return 0;
    case LWS_CALLBACK_ESTABLISHED:
    {
        memset(c, 0, sizeof *c);
        c->id = next_conn_id++;
        c->wsi = wsi;
        alias_string(c->alias, sizeof c->alias, next_alias++);
        c->next = conns;
        conns = c;
        ++dir_version;
        ms_log("games", "game server connected as %s", c->alias);
        struct ms_writer w;
        ms_writer_init(&w, 32, 1);
        ms_writer_u8(&w, 0);
        ms_writer_string(&w, c->alias);
        queue_message(c, &w);
        return 0;
    }
    case LWS_CALLBACK_RECEIVE:
    {
        if (c->closing)
            return 0;
        if (c->rx_len + len > MAX_RX)
            return -1;
        c->rx = realloc(c->rx, c->rx_len + len + 1);
        memcpy(c->rx + c->rx_len, in, len);
        c->rx_len += len;
        if (lws_remaining_packet_payload(wsi) == 0 && lws_is_final_fragment(wsi))
        {
            handle_message(c, c->rx, c->rx_len);
            free(c->rx);
            c->rx = NULL;
            c->rx_len = 0;
        }
        return 0;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE:
    {
        struct outmsg *m = c->out_head;
        if (m == NULL)
            return 0;
        c->out_head = m->next;
        if (c->out_head == NULL)
            c->out_tail = NULL;
        int written = lws_write(wsi, m->buf + LWS_PRE, m->len, LWS_WRITE_BINARY);
        free(m->buf);
        free(m);
        if (written < 0)
            return -1;
        if (c->out_head)
            lws_callback_on_writable(wsi);
        return 0;
    }
    case LWS_CALLBACK_CLOSED:
    {
        ms_log("games", "game server %s (%s) disconnected%s", c->alias,
               c->name[0] ? c->name : "unnamed",
               c->intentional ? ", it was shutting down" : "");
        c->closing = 1;
        ++dir_version;
        maybe_restart(c);
        for (int i = 0; i < SLOTS; ++i)
            if (c->uuids[i][0])
            {
                struct player *p = find_player(c->uuids[i]);
                if (p != NULL && p->conn == c)
                    player_left(p);
            }
        // joins still waiting for this connection are dropped by find_conn
        conn_free(c);
        return 0;
    }
    default:
        return 0;
    }
}

// ----------------------------------------------------------- housekeeping

void ms_games_kick_uuid(char const *uuid)
{
    struct kick *k = malloc(sizeof *k);
    ms_strlcpy(k->uuid, uuid, sizeof k->uuid);
    pthread_mutex_lock(&kick_lock);
    k->next = kick_head;
    kick_head = k;
    pthread_mutex_unlock(&kick_lock);
    if (context != NULL)
        lws_cancel_service(context);
}

void ms_games_tick()
{
    // queued kicks (password reset, logout everywhere)
    while (1)
    {
        pthread_mutex_lock(&kick_lock);
        struct kick *k = kick_head;
        if (k)
            kick_head = k->next;
        pthread_mutex_unlock(&kick_lock);
        if (k == NULL)
            break;
        struct player *p = find_player(k->uuid);
        if (p != NULL && p->conn != NULL)
            send_kick(p->conn, p->pos, p->nonce);
        free(k);
    }

    resolve_waiting();

    int64_t now_ms = ms_now_ms();
    restart_jobs_tick(now_ms);
    for (struct ms_gconn *c = conns; c; c = c->next)
        if (!c->closing &&
            (c->dir_sent != dir_version || now_ms - c->dir_sent_ms >= DIR_REFRESH_MS))
            send_directory(c);
    struct player *next;
    for (struct player *p = players; p; p = next)
    {
        next = p->next;
        if (p->flushing || p->version == p->flushed_version)
            continue;
        if (p->conn == NULL ||
            now_ms - p->last_flush_ms >= (int64_t)MS_CONFIG.flush_interval_ms)
            start_flush(p);
    }

    if (now_ms - last_count_log >= 15000)
    {
        last_count_log = now_ms;
        ms_log("players", "online %d", ms_games_online_count());
    }
}

int ms_games_online_count()
{
    int n = 0;
    for (struct player *p = players; p; p = p->next)
        if (p->conn != NULL)
            ++n;
    return n;
}

void ms_games_shutdown()
{
    int saved = 0;
    for (struct player *p = players; p; p = p->next)
    {
        if (p->version == p->flushed_version)
            continue;
        struct ms_db_rysteria r;
        snapshot(p, &r);
        if (MS_DB->rysteria_put(MS_DB, p->uuid, &r) == MS_DB_OK)
        {
            p->flushed_version = p->version;
            ++saved;
            if (!p->is_dev)
                ms_lb_push_rysteria(p->uuid, p->username, &r, p->pvp_best,
                                    &p->lb_count, p->lb_last, NULL);
        }
        else
            ms_log("games", "FAILED to save %s on shutdown", p->uuid);
        ms_db_rysteria_free(&r);
    }
    ms_log("games", "shutdown saved %d players", saved);
}
