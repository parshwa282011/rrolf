// A test of the Garden's snail run against a real server. A bot connects to a
// garden server (built with -DGARDEN) that was started with the test settings of
// tools/garden/run-bot-test.sh, plays, walks to the snail hole and checks what
// the server says: the run starts, the snail makes progress, it arrives, and the
// bot is rewarded.
//
//   tools/garden/run-bot-test.sh      builds everything and runs it
//
// The bot uses the game's own packet code (Shared/pb.c), so what it sends is
// what the real client sends. It never decodes the world.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libwebsockets.h>

#include <Shared/Crypto.h>
#include <Shared/MagicNumber.h>
#include <Shared/Rivet.h>
#include <Shared/Snail.h>
#include <Shared/StaticData.h>
#include <Shared/pb.h>

#define OUT_SIZE (LWS_PRE + 4096)

struct bot
{
    struct lws *wsi;
    char xff[16];
    uint8_t rx[1 << 20];
    size_t rx_size;
    uint8_t out[OUT_SIZE];
    uint8_t qv;
    int got_first;
    int packets;
    char uuid[500];
    char token[500];
    int last_in_game;
    uint64_t inv[rr_petal_id_max][rr_rarity_id_max];
    int have_account;
    uint64_t gallery[rr_mob_id_max][rr_rarity_id_max];
    int want_close;
    // the last snail state
    int have_snail;
    uint8_t phase, rarity, runs;
    double phase_since;
    uint16_t seq, route_cells, cells_left;
    uint8_t hp, waiting;
    float snail_x, snail_y, hole_x, hole_y;
    uint8_t last_event_type, last_event_rarity, last_event_count;
    // the last account reply (dev cheat answers)
    int replies;
    uint8_t reply_ok, reply_kind;
    char reply_message[96];
};

static struct bot bot;
static struct lws_context *context;
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

static double now()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static struct proto_bug begin(struct bot *b, uint8_t header)
{
    struct proto_bug e;
    proto_bug_init(&e, b->out + LWS_PRE);
    proto_bug_write_uint8(&e, b->qv, "qv");
    proto_bug_write_uint8(&e, header, "header");
    return e;
}

static void finish(struct bot *b, struct proto_bug *e)
{
    // what rr_websocket_send does, the encryption is off
    b->qv = rr_get_hash(b->qv);
    lws_write(b->wsi, b->out + LWS_PRE, e->current - e->start, LWS_WRITE_BINARY);
}

static void send_simple(struct bot *b, uint8_t header)
{
    struct proto_bug e = begin(b, header);
    finish(b, &e);
}

static void create_squad(struct bot *b)
{
    struct proto_bug e = begin(b, rr_serverbound_squad_join);
    proto_bug_write_uint8(&e, 2, "join type");
    finish(b, &e);
}


// how many (mob, rarity) pages of the gallery are empty
static int empty_gallery_pages(struct bot *b)
{
    int empty = 0;
    for (uint8_t id = 0; id < rr_mob_id_max; ++id)
        for (uint8_t rarity = 0; rarity < rr_rarity_id_max; ++rarity)
            empty += b->gallery[id][rarity] == 0;
    return empty;
}

// a cheat that has only the PIN in it, or gives petals
static void cheat_unlock(struct bot *b, char const *pin)
{
    struct proto_bug e = begin(b, rr_serverbound_dev_cheat);
    proto_bug_write_uint8(&e, rr_dev_cheat_unlock, "cheat type");
    proto_bug_write_string(&e, pin, 16, "pin");
    finish(b, &e);
}

static void cheat_give(struct bot *b, char const *pin, uint8_t id, uint8_t rarity, uint64_t count)
{
    struct proto_bug e = begin(b, rr_serverbound_dev_cheat);
    proto_bug_write_uint8(&e, rr_dev_cheat_give_petals, "cheat type");
    proto_bug_write_string(&e, pin, 16, "pin");
    proto_bug_write_uint8(&e, id, "id");
    proto_bug_write_uint8(&e, rarity, "rarity");
    proto_bug_write_varuint(&e, count, "count");
    finish(b, &e);
}

// what a -DCTF_TEST server allows
static void teleport(struct bot *b, float x, float y)
{
    struct proto_bug e = begin(b, rr_serverbound_dev_cheat);
    proto_bug_write_uint8(&e, rr_dev_cheat_teleport, "cheat type");
    proto_bug_write_string(&e, "09282011", 16, "pin"); // every cheat has the PIN
    proto_bug_write_float32(&e, x, "teleport x");
    proto_bug_write_float32(&e, y, "teleport y");
    finish(b, &e);
}

static void move(struct bot *b, uint8_t flags)
{
    struct proto_bug e = begin(b, rr_serverbound_input);
    proto_bug_write_uint8(&e, flags, "movement kb flags");
    finish(b, &e);
}

// ---- receiving

// the part of the update packet that says whether the player is in the game
static void parse_update(struct bot *b, struct proto_bug *d)
{
    char text[40];
    proto_bug_read_uint8(d, "kick vote");
    for (uint32_t i = 0; i < RR_SQUAD_MEMBER_COUNT; ++i)
    {
        if (!proto_bug_read_uint8(d, "bitbit"))
            continue;
        proto_bug_read_uint8(d, "ready");
        proto_bug_read_uint8(d, "disconnected");
        proto_bug_read_uint8(d, "blocked");
        proto_bug_read_uint8(d, "is_dev");
        proto_bug_read_uint8(d, "kick votes");
        proto_bug_read_varuint(d, "level");
        proto_bug_read_string(d, text, 16, "nickname");
        proto_bug_read_string(d, text, 16, "role");
        proto_bug_read_string(d, text, 37, "uuid");
        proto_bug_read_string(d, text, 20, "discord");
        for (uint8_t j = 0; j < RR_MAX_SLOT_COUNT * 2; ++j)
        {
            proto_bug_read_uint8(d, "id");
            proto_bug_read_uint8(d, "rar");
        }
    }
    proto_bug_read_uint8(d, "sqidx");
    proto_bug_read_uint8(d, "sqown");
    proto_bug_read_uint8(d, "sqpos");
    proto_bug_read_uint8(d, "private");
    proto_bug_read_uint8(d, "expose_code");
    proto_bug_read_uint8(d, "biome");
    proto_bug_read_string(d, text, 16, "squad code");
    uint64_t afk = proto_bug_read_varuint(d, "afk_ticks");
    if (afk > RR_AFK_WARNING)
        proto_bug_read_string(d, text, 7, "afk_challenge");
    b->last_in_game = proto_bug_read_uint8(d, "in game");
}

static void parse_snail_state(struct bot *b, struct proto_bug *d)
{
    b->have_snail = 1;
    b->seq = proto_bug_read_varuint(d, "snail event seq");
    for (uint8_t e = 0; e < RR_SNAIL_EVENT_HISTORY; ++e)
    {
        uint8_t type = proto_bug_read_uint8(d, "snail event");
        uint8_t rarity = proto_bug_read_uint8(d, "snail event r");
        uint8_t count = proto_bug_read_uint8(d, "snail event n");
        if (e == RR_SNAIL_EVENT_HISTORY - 1)
        {
            b->last_event_type = type;
            b->last_event_rarity = rarity;
            b->last_event_count = count;
        }
    }
    b->runs = proto_bug_read_uint8(d, "snail runs");
    if (b->runs > 0)
    {
        b->hole_x = proto_bug_read_float32(d, "hole x");
        b->hole_y = proto_bug_read_float32(d, "hole y");
        for (uint8_t i = 0; i < b->runs && i < RR_SNAIL_MAX_RUNS; ++i)
        {
            // the first one is the one the test follows
            uint8_t rarity = proto_bug_read_uint8(d, "snail rarity");
            uint16_t route = proto_bug_read_varuint(d, "snail route");
            uint16_t left = proto_bug_read_varuint(d, "snail left");
            uint8_t hp = proto_bug_read_uint8(d, "snail hp");
            uint8_t waiting = proto_bug_read_uint8(d, "snail waiting");
            float x = proto_bug_read_float32(d, "snail x");
            float y = proto_bug_read_float32(d, "snail y");
            if (i == 0)
            {
                b->rarity = rarity;
                b->route_cells = route;
                b->cells_left = left;
                b->hp = hp;
                b->waiting = waiting;
                b->snail_x = x;
                b->snail_y = y;
            }
        }
    }
    // the phase the test thinks in: running while a snail is going, then how
    // the last one ended, and idle again after the 15 seconds the result stays up
    if (b->runs > 0)
    {
        b->phase = rr_snail_phase_running;
        b->phase_since = 0;
    }
    else if (b->phase == rr_snail_phase_running)
    {
        b->phase = b->last_event_type == rr_snail_event_arrived ? rr_snail_phase_arrived
                                                                : rr_snail_phase_failed;
        b->phase_since = now();
    }
    else if ((b->phase == rr_snail_phase_arrived || b->phase == rr_snail_phase_failed) &&
             now() - b->phase_since > 16)
        b->phase = rr_snail_phase_idle;
}

static void on_packet(struct bot *b, uint8_t *data, size_t size)
{
    struct proto_bug d;
    proto_bug_init(&d, data);
    proto_bug_set_bound(&d, data + size);
    if (!b->got_first)
    {
        b->got_first = 1;
        uint64_t verification = proto_bug_read_uint64(&d, "verification");
        proto_bug_read_uint32(&d, "useless bytes");
        proto_bug_read_uint64(&d, "c encryption key");
        proto_bug_read_uint64(&d, "s encryption key");
        b->qv = RR_SECRET8;
        struct proto_bug e;
        proto_bug_init(&e, b->out + LWS_PRE);
        proto_bug_write_uint64(&e, rr_get_rand(), "useless bytes");
        proto_bug_write_uint64(&e, verification, "verification");
        proto_bug_write_string(&e, b->token, 300, "rivet token");
        proto_bug_write_string(&e, b->uuid, 100, "rivet uuid");
        proto_bug_write_string(&e, "", 100, "oauth2 code");
        proto_bug_write_varuint(&e, 0, "dev_flag");
        finish(b, &e);
        return;
    }
    ++b->packets;
    uint8_t header = proto_bug_read_uint8(&d, "header");
    if (header == rr_clientbound_oauth2_data)
    {
        char name[500];
        proto_bug_read_string(&d, b->uuid, sizeof b->uuid, "uuid");
        proto_bug_read_string(&d, b->token, sizeof b->token, "token");
        proto_bug_read_string(&d, name, sizeof name, "name");
    }
    else if (header == rr_clientbound_account_result)
    {
        proto_bug_read_float64(&d, "xp");
        memset(b->inv, 0, sizeof b->inv);
        uint8_t id;
        while ((id = proto_bug_read_uint8(&d, "id")))
        {
            uint8_t rarity = proto_bug_read_uint8(&d, "rarity");
            uint64_t count = proto_bug_read_varuint(&d, "count");
            if (id < rr_petal_id_max && rarity < rr_rarity_id_max)
                b->inv[id][rarity] = count;
        }
        // failed crafts, then the mob gallery (ids from 1)
        while ((id = proto_bug_read_uint8(&d, "id")))
        {
            proto_bug_read_uint8(&d, "rarity");
            proto_bug_read_varuint(&d, "count");
        }
        memset(b->gallery, 0, sizeof b->gallery);
        while ((id = proto_bug_read_uint8(&d, "id")))
        {
            uint8_t rarity = proto_bug_read_uint8(&d, "rarity");
            uint64_t count = proto_bug_read_varuint(&d, "count");
            if (id - 1 < rr_mob_id_max && rarity < rr_rarity_id_max)
                b->gallery[id - 1][rarity] = count;
        }
        b->have_account = 1;
    }
    else if (header == rr_clientbound_update)
        parse_update(b, &d);
    else if (header == rr_clientbound_snail_state)
        parse_snail_state(b, &d);
    else if (header == rr_clientbound_account_reply)
    {
        char uuid[100] = {0}, token[300] = {0}, code[100] = {0};
        b->reply_ok = proto_bug_read_uint8(&d, "ok");
        b->reply_kind = proto_bug_read_uint8(&d, "kind");
        proto_bug_read_string(&d, b->reply_message, sizeof b->reply_message - 1, "message");
        proto_bug_read_string(&d, uuid, sizeof uuid - 1, "uuid");
        proto_bug_read_string(&d, token, sizeof token - 1, "token");
        proto_bug_read_string(&d, code, sizeof code - 1, "code");
        ++b->replies;
    }
}

static int callback(struct lws *wsi, enum lws_callback_reasons reason,
                    void *user, void *in, size_t len)
{
    struct bot *b = (struct bot *)user;
    switch (reason)
    {
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    {
        // the server refuses connections that do not come through a proxy
        unsigned char **p = (unsigned char **)in;
        unsigned char *end = (*p) + len;
        if (lws_add_http_header_by_name(wsi, (unsigned char *)"X-Forwarded-For:",
                                        (unsigned char *)b->xff,
                                        strlen(b->xff), p, end))
            return -1;
        break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (b->rx_size + len > sizeof b->rx)
            return -1;
        memcpy(b->rx + b->rx_size, in, len);
        b->rx_size += len;
        if (lws_is_final_fragment(wsi))
        {
            on_packet(b, b->rx, b->rx_size);
            b->rx_size = 0;
        }
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        // a bot that is told to hang up does it here
        if (b->want_close)
            return -1;
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        printf("connection error: %s\n",
               in ? (char *)in : "(none)");
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        printf("connection closed\n");
        b->wsi = NULL;
        break;
    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {{"g", callback, 0, 0},
                                           {NULL, NULL, 0, 0}};


static int connect_bot(struct bot *b, int port, char const *xff)
{
    struct lws_client_connect_info c;
    memset(&c, 0, sizeof c);
    c.context = context;
    c.address = "127.0.0.1";
    c.port = port;
    c.path = "/";
    c.host = "127.0.0.1";
    c.origin = "127.0.0.1";
    c.protocol = "g";
    c.userdata = b;
    c.pwsi = &b->wsi;
    snprintf(b->xff, sizeof b->xff, "%s", xff);
    b->got_first = 0;
    b->packets = 0;
    b->rx_size = 0;
    b->have_account = 0;
    b->last_in_game = 0;
    b->want_close = 0;
    return lws_client_connect_via_info(&c) != NULL;
}

// ---- the test

static void pump(double seconds)
{
    double end = now() + seconds;
    while (now() < end)
        lws_service(context, 10);
}

// waits for a condition and says how long it took
#define WAIT(cond, seconds, what)                                              \
    do                                                                         \
    {                                                                          \
        double _end = now() + (seconds);                                       \
        while (!(cond) && now() < _end)                                        \
            lws_service(context, 10);                                          \
        CHECK((cond), "timed out waiting for: %s", what);                      \
    } while (0)

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 1236;
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.pt_serv_buf_size = 1 << 20;
    lws_set_log_level(LLL_ERR, NULL);
    context = lws_create_context(&info);
    struct bot *b = &bot;
    if (!connect_bot(b, port, "127.0.0.20"))
    {
        printf("could not connect to port %d\n", port);
        return 2;
    }
    printf("connecting\n");
    WAIT(b->packets >= 2, 15, "the bot is verified");

    printf("playing\n");
    create_squad(b);
    pump(0.6);
    send_simple(b, rr_serverbound_squad_ready);
    WAIT(b->last_in_game != 0, 6, "the bot is in the game");
    CHECK(b->last_in_game == 2 || b->last_in_game == 1, "in game %d", b->last_in_game);

    pump(1);
    printf("the dev PIN\n");
    {
        int replies = b->replies;
        cheat_unlock(b, "0000");
        WAIT(b->replies > replies, 3, "a wrong PIN is answered");
        CHECK(!b->reply_ok && b->reply_kind == rr_account_request_dev_unlock, "as a wrong PIN: ok %d kind %d '%s'", b->reply_ok, b->reply_kind, b->reply_message);
        uint64_t stingers = b->inv[rr_petal_id_stinger][rr_rarity_id_epic];
        pump(1.2); // a guess a second
        cheat_give(b, "0000", rr_petal_id_stinger, rr_rarity_id_epic, 5);
        pump(0.8);
        CHECK(b->inv[rr_petal_id_stinger][rr_rarity_id_epic] == stingers, "a cheat with a wrong PIN does nothing");
        replies = b->replies;
        cheat_unlock(b, "09282011");
        WAIT(b->replies > replies, 3, "the right PIN is answered");
        CHECK(b->reply_ok && b->reply_kind == rr_account_request_dev_unlock, "as unlocked: ok %d kind %d '%s'", b->reply_ok, b->reply_kind, b->reply_message);
        cheat_give(b, "09282011", rr_petal_id_nazar, rr_rarity_id_epic, 3);
        WAIT(b->inv[rr_petal_id_nazar][rr_rarity_id_epic] == 3, 4, "a cheat with the right PIN works (3 epic nazars)");
        printf("unlocking fills the gallery\n");
        WAIT(empty_gallery_pages(b) == 0, 4, "every mob has at least one page at every rarity");
    }
    uint64_t snails_before = b->gallery[rr_mob_id_snail][3];
    uint64_t mint_before = b->inv[rr_petal_id_mint][1] + b->inv[rr_petal_id_mint][2] + b->inv[rr_petal_id_mint][3];
    printf("the snail waits until somebody is there\n");
    WAIT(b->have_snail && b->phase == rr_snail_phase_running, 30, "a run starts");
    CHECK(b->waiting, "the snail is asleep");
    CHECK(b->rarity >= rr_rarity_id_epic, "it is at least epic (%d)", b->rarity);
    float x0 = b->snail_x, y0 = b->snail_y;
    uint16_t left0 = b->cells_left;
    CHECK(x0 > 0 && y0 > 0 && b->route_cells > 0, "it is somewhere (%.0f, %.0f), %u cells from the hole", x0, y0, b->route_cells);
    pump(5);
    CHECK(b->waiting && b->snail_x == x0 && b->snail_y == y0 && b->cells_left == left0,
          "it has not moved with nobody near (%.0f, %.0f)", b->snail_x, b->snail_y);
    printf("the bot goes to it\n");
    teleport(&bot, x0 + 400, y0);
    WAIT(!b->waiting, 8, "the snail wakes up");
    // and then to the snail hole, which is cell (20, 21) of the map, a cell is
    // 2048 units, so that the bot is there when the snail arrives
    printf("waiting at the snail hole\n");
    teleport(&bot, 20.5f * 2048, 22.5f * 2048);
    pump(0.5);
    double wait_end = now() + 240;
    double last_log = 0;
    while (now() < wait_end && !(b->have_snail && b->phase == rr_snail_phase_arrived))
    {
        lws_service(context, 100);
        if (b->have_snail && b->phase == rr_snail_phase_running && now() - last_log > 10)
        {
            last_log = now();
            printf("  snail %d%% hp, %u of %u cells left, at %.0f %.0f\n", b->hp,
                   b->cells_left, b->route_cells, b->snail_x, b->snail_y);
        }
    }

    printf("the snail run\n");
    WAIT(b->have_snail, 3, "a snail state arrived");
    uint16_t route = b->route_cells;
    (void)route;
    CHECK(b->phase == rr_snail_phase_running || b->phase == rr_snail_phase_arrived,
          "a run is going or has ended, phase %d", b->phase);
    if (b->phase == rr_snail_phase_running)
    {
        CHECK(route > 0, "route cells %u", route);
        CHECK(b->hole_x > 0 && b->hole_y > 0, "the hole is somewhere (%.0f, %.0f)", b->hole_x, b->hole_y);
        uint16_t first_left = b->cells_left;
        pump(5);
        CHECK(b->phase == rr_snail_phase_arrived || b->cells_left <= first_left, "the snail makes progress (%u -> %u)", first_left, b->cells_left);
    }
    WAIT(b->phase == rr_snail_phase_arrived || b->phase == rr_snail_phase_failed, 240, "the run ends");
    CHECK(b->phase == rr_snail_phase_arrived, "the snail arrived, phase %d", b->phase);
    CHECK(b->last_event_type == rr_snail_event_arrived, "the last event says so, %d", b->last_event_type);
    CHECK(b->last_event_count >= 1, "the bot was rewarded, count %d", b->last_event_count);
    pump(1.5); // the account result follows
    uint64_t mint_after = b->inv[rr_petal_id_mint][1] + b->inv[rr_petal_id_mint][2] + b->inv[rr_petal_id_mint][3];
    CHECK(mint_after > mint_before, "a mint was added to the inventory (%llu -> %llu)",
          (unsigned long long)mint_before, (unsigned long long)mint_after);
    printf("  mint by rarity: u %llu r %llu e %llu\n",
           (unsigned long long)b->inv[rr_petal_id_mint][1], (unsigned long long)b->inv[rr_petal_id_mint][2],
           (unsigned long long)b->inv[rr_petal_id_mint][3]);
    WAIT(b->phase == rr_snail_phase_idle, 25, "the event goes back to idle");
    CHECK(b->gallery[rr_mob_id_snail][3] == snails_before + 1,
          "the rewarded bot has the snail in its gallery (%llu -> %llu)",
          (unsigned long long)snails_before, (unsigned long long)b->gallery[rr_mob_id_snail][3]);

    printf("the gallery was saved with the account\n");
    b->want_close = 1;
    lws_callback_on_writable(b->wsi);
    pump(1.5);
    CHECK(b->wsi == NULL, "the bot hung up");
    CHECK(connect_bot(b, port, "127.0.0.20"), "and connects again as the same player");
    memset(b->gallery, 0, sizeof b->gallery);
    WAIT(b->have_account, 10, "its account is sent again");
    CHECK(empty_gallery_pages(b) == 0, "the gallery is still full, %d pages are empty", empty_gallery_pages(b));
    CHECK(b->gallery[rr_mob_id_snail][3] == snails_before + 1, "and so is the snail (%llu)", (unsigned long long)b->gallery[rr_mob_id_snail][3]);

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
