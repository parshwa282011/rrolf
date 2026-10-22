// A test of capture the flag against a real server. Two bots connect to a CTF
// server built with -DCTF_TEST, link their squads, play a whole match and check
// what the server says at every step.
//
//   tools/ctf/run-bot-test.sh      builds everything and runs it
//
// The bots use the game's own packet code (Shared/pb.c), so what they send is
// what the real client sends. They never decode the world, only the squad list
// and the capture the flag state, and they move by the test only teleport
// command.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libwebsockets.h>

#include <Shared/Crypto.h>
#include <Shared/Ctf.h>
#include <Shared/MagicNumber.h>
#include <Shared/Rivet.h>
#include <Shared/StaticData.h>
#include <Shared/pb.h>

#define BOTS 2
#define OUT_SIZE (LWS_PRE + 4096)

struct flag_state
{
    uint8_t state;
    uint8_t holder;
    float x;
    float y;
};

struct team_state
{
    uint8_t eliminated, squad, members, held, banked;
    uint8_t point_set[3];
    float px[3], py[3];
    uint8_t setup[3];
};

struct bot
{
    char name[4];
    char xff[16];
    struct lws *wsi;
    uint8_t rx[1 << 20];
    size_t rx_size;
    uint8_t out[OUT_SIZE];
    uint8_t qv;
    int got_first;
    int packets;
    char code[16];
    // who the server says this bot is, to connect again as the same player
    char uuid[500];
    char token[500];
    int want_close;
    // the inventory the server last sent
    uint64_t inv[rr_petal_id_max][rr_rarity_id_max];
    int have_account;
    // the last capture the flag state
    int have_state;
    uint8_t phase, team_count, my_team, flags, winner, message, message_arg;
    uint32_t ticks;
    uint16_t seq;
    struct team_state teams[4];
    struct flag_state flag[12];
};

static struct bot bots[BOTS];
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

// ---- sending

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

static void ctf_create(struct bot *b, char const *code)
{
    struct proto_bug e = begin(b, rr_serverbound_ctf_create);
    proto_bug_write_uint8(&e, code != NULL, "ctf count");
    if (code != NULL)
        proto_bug_write_string(&e, code, 16, "ctf code");
    finish(b, &e);
}

static void place_flag(struct bot *b, uint8_t index, float x, float y)
{
    struct proto_bug e = begin(b, rr_serverbound_ctf_place_flag);
    proto_bug_write_uint8(&e, index, "flag index");
    proto_bug_write_float32(&e, x, "flag x");
    proto_bug_write_float32(&e, y, "flag y");
    finish(b, &e);
}

static void teleport(struct bot *b, float x, float y)
{
    struct proto_bug e = begin(b, rr_serverbound_dev_cheat);
    proto_bug_write_uint8(&e, rr_dev_cheat_teleport, "cheat type");
    proto_bug_write_float32(&e, x, "teleport x");
    proto_bug_write_float32(&e, y, "teleport y");
    finish(b, &e);
}

// ---- receiving

static void parse_squad_dump(struct bot *b, struct proto_bug *d)
{
    proto_bug_read_uint8(d, "is_dev");
    proto_bug_read_uint8(d, "kick vote");
    for (uint32_t s = 0; s < RR_SQUAD_COUNT; ++s)
    {
        uint8_t members = 0;
        for (uint32_t i = 0; i < RR_SQUAD_MEMBER_COUNT; ++i)
        {
            if (!proto_bug_read_uint8(d, "bitbit"))
                continue;
            ++members;
            char text[40];
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
        proto_bug_read_uint8(d, "sqown");
        proto_bug_read_uint8(d, "private");
        proto_bug_read_uint8(d, "expose_code");
        proto_bug_read_uint8(d, "biome");
        char code[16] = {0};
        proto_bug_read_string(d, code, 16, "squad code");
        // the only squad with someone in it whose code is shown is ours
        if (members > 0 && strcmp(code, "(private)") != 0)
        {
            char *dash = strchr(code, '-');
            snprintf(b->code, sizeof b->code, "%s", dash ? dash + 1 : code);
        }
    }
}

static void parse_ctf_state(struct bot *b, struct proto_bug *d)
{
    b->have_state = 1;
    b->phase = proto_bug_read_uint8(d, "ctf phase");
    b->team_count = proto_bug_read_uint8(d, "ctf teams");
    b->my_team = proto_bug_read_uint8(d, "ctf my team");
    b->flags = proto_bug_read_uint8(d, "ctf flags");
    b->winner = proto_bug_read_uint8(d, "ctf winner");
    b->message = proto_bug_read_uint8(d, "ctf message");
    b->message_arg = proto_bug_read_uint8(d, "ctf arg");
    b->ticks = proto_bug_read_varuint(d, "ctf ticks");
    b->seq = proto_bug_read_varuint(d, "ctf event seq");
    for (int e = 0; e < RR_CTF_EVENT_HISTORY; ++e)
    {
        proto_bug_read_uint8(d, "ctf event");
        proto_bug_read_uint8(d, "ctf event a");
        proto_bug_read_uint8(d, "ctf event b");
        proto_bug_read_uint8(d, "ctf event flag");
    }
    if (!(b->flags >> 3 & 1))
        return;
    for (int t = 0; t < b->team_count; ++t)
    {
        struct team_state *team = &b->teams[t];
        team->eliminated = proto_bug_read_uint8(d, "ctf team out");
        team->squad = proto_bug_read_uint8(d, "ctf team squad");
        team->members = proto_bug_read_uint8(d, "ctf team members");
        team->held = proto_bug_read_uint8(d, "ctf held");
        team->banked = proto_bug_read_uint8(d, "ctf banked");
        for (int i = 0; i < 3; ++i)
        {
            team->point_set[i] = proto_bug_read_uint8(d, "ctf point set");
            team->px[i] = proto_bug_read_float32(d, "ctf point x");
            team->py[i] = proto_bug_read_float32(d, "ctf point y");
            team->setup[i] = proto_bug_read_uint8(d, "ctf setup");
        }
    }
    if (b->flags >> 5 & 1)
        for (int f = 0; f < b->team_count * 3; ++f)
        {
            b->flag[f].state = proto_bug_read_uint8(d, "ctf flag state");
            b->flag[f].holder = proto_bug_read_uint8(d, "ctf flag holder");
            b->flag[f].x = proto_bug_read_float32(d, "ctf flag x");
            b->flag[f].y = proto_bug_read_float32(d, "ctf flag y");
        }
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
        // the identity the master server gave, kept to come back as the same
        // player
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
        b->have_account = 1;
    }
    else if (header == rr_clientbound_squad_dump)
        parse_squad_dump(b, &d);
    else if (header == rr_clientbound_ctf_state)
        parse_ctf_state(b, &d);
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
        printf("connection error for %s: %s\n", b->name,
               in ? (char *)in : "(none)");
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        printf("connection of %s closed\n", b->name);
        b->wsi = NULL;
        break;
    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {{"g", callback, 0, 0},
                                           {NULL, NULL, 0, 0}};

static int connect_bot(struct bot *b, int port)
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
    b->got_first = 0;
    b->packets = 0;
    b->rx_size = 0;
    b->want_close = 0;
    b->have_account = 0;
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
    for (int i = 0; i < BOTS; ++i)
    {
        struct bot *b = &bots[i];
        snprintf(b->name, sizeof b->name, "%c", 'A' + i);
        snprintf(b->xff, sizeof b->xff, "127.0.0.%d", 10 + i);
        b->my_team = 255;
        if (!connect_bot(b, port))
        {
            printf("could not connect to port %d\n", port);
            return 2;
        }
    }
    struct bot *A = &bots[0], *B = &bots[1];

    printf("connecting\n");
    WAIT(A->packets >= 2 && B->packets >= 2, 15, "both bots verified");

    printf("making private squads\n");
    create_squad(A);
    create_squad(B);
    WAIT(A->code[0] && B->code[0], 6, "both squads have a code");
    printf("  squad codes: A %s, B %s\n", A->code, B->code);

    printf("lobby\n");
    WAIT(A->have_state, 3, "a capture the flag state");
    CHECK(A->phase == rr_ctf_phase_lobby && A->team_count == 0, "phase %d", A->phase);
    pump(0.6); // squad actions have a cooldown
    send_simple(A, rr_serverbound_ctf_start);
    pump(0.4);
    CHECK(A->message == rr_ctf_message_not_host, "message %d, nothing is linked yet", A->message);
    pump(3.2);
    ctf_create(A, "zzzzzz");
    WAIT(A->message == rr_ctf_message_bad_code, 2, "a bad code is refused");
    pump(3.2);
    ctf_create(A, A->code);
    WAIT(A->message == rr_ctf_message_same_squad, 2, "the host's own squad is refused");
    pump(3.2);
    ctf_create(A, B->code);
    WAIT(A->team_count == 2, 3, "two squads linked");
    CHECK(A->message == rr_ctf_message_linked && A->message_arg == 2, "message %d", A->message);
    CHECK(A->my_team == 0, "the host is team 0, got %d", A->my_team);
    WAIT(B->team_count == 2, 2, "the other bot sees the link");
    CHECK(B->my_team == 1, "the other squad is team 1, got %d", B->my_team);
    CHECK(A->flags >> 1 & 1, "A is told it is the host");
    CHECK(A->flags >> 2 & 1, "A is told it can start");
    CHECK(!(B->flags >> 1 & 1), "B is not the host");
    CHECK(A->teams[0].members == 1 && A->teams[1].members == 1, "one player in each");

    printf("start and placement\n");
    pump(3.2);
    send_simple(B, rr_serverbound_ctf_start);
    pump(0.4);
    CHECK(A->phase == rr_ctf_phase_lobby, "a player who is not the host can't start");
    pump(3);
    send_simple(A, rr_serverbound_ctf_start);
    WAIT(A->phase == rr_ctf_phase_placement && B->phase == rr_ctf_phase_placement, 3,
         "placement");
    CHECK(!(A->flags & 1), "the walls are up");
    CHECK(!(A->flags >> 5 & 1), "no flags yet");
    pump(0.5);
    place_flag(A, 0, 1, 1);
    WAIT(A->message == rr_ctf_message_bad_spot, 2, "a flag in a wall is refused");
    CHECK(A->flags >> 4 & 1, "A leads its squad");

    printf("the walls stay up\n");
    WAIT(A->phase == rr_ctf_phase_farm, 8, "the farming phase");
    CHECK(A->flags >> 5 & 1, "the flags exist now");
    CHECK(A->teams[0].point_set[0] && A->teams[0].point_set[2], "default flag points were set");
    CHECK(A->flag[0].state == rr_ctf_flag_standing && A->flag[3].state == rr_ctf_flag_standing,
          "flags are standing");
    CHECK(A->teams[0].held == 3 && A->teams[1].held == 3, "3 flags each");
    float a0x = A->flag[0].x, a0y = A->flag[0].y;
    CHECK(a0x > 0 && a0y > 0, "flag 0 is somewhere");
    CHECK(A->flag[3].x > A->flag[0].x + 20000, "the teams' flags are far apart (%.0f, %.0f)",
          A->flag[0].x, A->flag[3].x);

    printf("war\n");
    WAIT(A->phase == rr_ctf_phase_war && B->phase == rr_ctf_phase_war, 8, "the war");
    CHECK(A->flags & 1 && B->flags & 1, "the walls dropped, both are told");

    printf("a dead player can't take a flag, and comes back\n");
    send_simple(B, rr_serverbound_ctf_respawn);
    pump(0.25);
    teleport(B, A->flag[0].x, A->flag[0].y);
    pump(0.4);
    CHECK(B->flag[0].state == rr_ctf_flag_standing, "a dead player did not take the flag");
    pump(2.0); // the respawn takes about a second
    teleport(B, A->flag[0].x, A->flag[0].y);
    WAIT(B->flag[0].state == rr_ctf_flag_carried, 3, "the respawned player takes it");
    // let it go again for the main test: die while carrying
    send_simple(B, rr_serverbound_ctf_respawn);
    WAIT(B->flag[0].state == rr_ctf_flag_dropped, 3, "a flag drops when its carrier dies");
    CHECK(B->flag[0].holder == 0, "a dropped flag is still A's");
    // A's player touches it: it goes home
    pump(1.5);
    teleport(A, B->flag[0].x, B->flag[0].y);
    WAIT(B->flag[0].state == rr_ctf_flag_standing, 3, "its own team sends it home");
    CHECK(B->flag[0].x == B->teams[0].px[0] && B->flag[0].y == B->teams[0].py[0],
          "it is back on its point");

    printf("a player who reconnects keeps their petals and their place\n");
    CHECK(B->have_account, "B was sent its account");
    CHECK(B->inv[rr_petal_id_basic][rr_rarity_id_epic] == RR_CTF_START_PETALS,
          "B starts with %d Epic Basic, has %llu", RR_CTF_START_PETALS,
          (unsigned long long)B->inv[rr_petal_id_basic][rr_rarity_id_epic]);
    {
        // crafting uses up some of the five, so a reset to five is visible
        struct proto_bug e = begin(B, rr_serverbound_petals_craft);
        proto_bug_write_uint8(&e, rr_petal_id_basic, "craft id");
        proto_bug_write_uint8(&e, rr_rarity_id_epic, "craft rarity");
        proto_bug_write_varuint(&e, 5, "craft count");
        finish(B, &e);
    }
    pump(0.5);
    B->want_close = 1;
    lws_callback_on_writable(B->wsi);
    WAIT(B->wsi == NULL, 3, "B hangs up");
    pump(0.5);
    CHECK(connect_bot(B, port), "B connects again");
    WAIT(B->have_account, 8, "B is sent its account again");
    uint64_t kept = B->inv[rr_petal_id_basic][rr_rarity_id_epic];
    CHECK(kept < RR_CTF_START_PETALS, "B kept what was left after crafting: %llu of %d",
          (unsigned long long)kept, RR_CTF_START_PETALS);
    WAIT(B->my_team == 1 && B->phase == rr_ctf_phase_war, 4, "B is back on its team in the war");
    pump(1.0);

    printf("B takes a flag of A and banks it\n");
    pump(1.5);
    teleport(B, A->flag[0].x, A->flag[0].y);
    WAIT(B->flag[0].state == rr_ctf_flag_carried, 3, "flag 0 of A is carried");
    CHECK(B->flag[0].holder == 0, "still held by A while carried");
    CHECK(B->teams[0].held == 3, "A still holds 3 while it is only carried");
    teleport(B, B->teams[1].px[0], B->teams[1].py[0]);
    WAIT(B->flag[0].state == rr_ctf_flag_banked, 3, "the flag is banked");
    CHECK(B->flag[0].holder == 1, "banked for B");
    WAIT(B->teams[0].eliminated, 2, "A is out at once");
    CHECK(B->teams[0].held == 2 && B->teams[1].held == 4, "A holds %d and B holds %d",
          B->teams[0].held, B->teams[1].held);
    CHECK(B->phase == rr_ctf_phase_war && B->winner == 255, "no winner yet");
    CHECK(A->teams[0].eliminated, "A is told as well");

    printf("B takes the last two\n");
    for (int f = 1; f <= 2; ++f)
    {
        teleport(B, B->flag[f].x, B->flag[f].y);
        WAIT(B->flag[f].state == rr_ctf_flag_carried, 3, "a flag is carried");
        teleport(B, B->teams[1].px[0], B->teams[1].py[0]);
        WAIT(B->flag[f].state == rr_ctf_flag_banked, 3, "a flag is banked");
    }
    WAIT(B->phase == rr_ctf_phase_ended, 3, "the match ends");
    CHECK(B->winner == 1 && A->winner == 1, "B won (%d, %d)", B->winner, A->winner);

    printf("back to the lobby\n");
    WAIT(A->phase == rr_ctf_phase_lobby && B->phase == rr_ctf_phase_lobby, 6, "the lobby");
    CHECK(A->team_count == 0, "the squads are free again");
    CHECK(!(A->flags & 1), "the walls are back up");

    printf("\n%d checks, %d failed\n", checks, failures);
    lws_context_destroy(context);
    return failures != 0;
}
