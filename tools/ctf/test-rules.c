// Tests for the capture the flag rules in Server/CtfRules.c. They need nothing
// from the server, build and run with:
//
//   cc -I. -o /tmp/ctf-test tools/ctf/test-rules.c Server/CtfRules.c && /tmp/ctf-test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Server/Ctf.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        ++checks;                                                              \
        if (!(cond))                                                           \
        {                                                                      \
            ++failures;                                                        \
            printf("FAIL line %d: %s  ", __LINE__, #cond);                     \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static struct rr_ctf_match m;
static struct rr_ctf_player players[RR_MAX_CLIENT_COUNT];

// teams sit on a row 10000 apart, their three points 1000 apart
static float px(uint8_t team, uint8_t point) { return team * 10000.0f + point * 1000; }
static float py(uint8_t team, uint8_t point) { (void)team; (void)point; return 0; }

static void start_match(uint8_t teams)
{
    rr_ctf_match_reset(&m);
    memset(players, 0, sizeof players);
    m.team_count = teams;
    for (uint8_t t = 0; t < teams; ++t)
    {
        m.teams[t].active = 1;
        m.teams[t].squad = t;
        for (uint8_t i = 0; i < 3; ++i)
            rr_ctf_set_point(&m, t, i, px(t, i), py(t, i));
    }
    CHECK(rr_ctf_all_points_set(&m), "points set");
    rr_ctf_create_flags(&m);
}

// player slot p plays for a team, standing somewhere
static void place(uint8_t p, uint8_t team, float x, float y)
{
    players[p] = (struct rr_ctf_player){
        .present = 1, .alive = 1, .located = 1, .team = team, .x = x, .y = y};
}

static void step(int ticks)
{
    for (int i = 0; i < ticks; ++i)
        rr_ctf_step(&m, players);
}

static struct rr_ctf_flag *flag(uint8_t owner, uint8_t index)
{
    return &m.flags[owner * 3 + index];
}

// walk player p to a flag of a team and bring it to a point of their own
static void steal_and_bank(uint8_t p, uint8_t team, uint8_t from, uint8_t index)
{
    place(p, team, flag(from, index)->x, flag(from, index)->y);
    step(1);
    CHECK(flag(from, index)->state == rr_ctf_flag_carried,
          "flag %d/%d should be carried, state %d", from, index,
          flag(from, index)->state);
    place(p, team, px(team, 1), py(team, 1));
    step(1);
}

static void test_start()
{
    start_match(2);
    CHECK(rr_ctf_held(&m, 0) == 3 && rr_ctf_held(&m, 1) == 3, "3 each");
    CHECK(rr_ctf_held(&m, 2) == 0, "inactive team holds nothing: %d", rr_ctf_held(&m, 2));
    CHECK(m.teams[0].point_flag[2] == 2, "flag on point");
    step(10);
    CHECK(!m.teams[0].eliminated && !m.teams[1].eliminated, "nobody out yet");
    CHECK(m.winner == RR_CTF_NONE, "no winner");
    // nobody near: nothing happens
    place(0, 0, 5000, 5000);
    step(100);
    CHECK(flag(1, 0)->state == rr_ctf_flag_standing, "still standing");
}

static void test_own_flags_are_safe()
{
    start_match(2);
    place(0, 0, flag(0, 0)->x, flag(0, 0)->y);
    step(5);
    CHECK(flag(0, 0)->state == rr_ctf_flag_standing, "own flag can't be taken");
}

static void test_two_team_game()
{
    start_match(2);
    uint8_t seq0 = m.event_seq;
    steal_and_bank(0, 0, 1, 0);
    CHECK(flag(1, 0)->state == rr_ctf_flag_banked && flag(1, 0)->holder == 0,
          "banked for team 0");
    CHECK(rr_ctf_held(&m, 0) == 4, "team 0 holds 4: %d", rr_ctf_held(&m, 0));
    CHECK(rr_ctf_held(&m, 1) == 2, "team 1 holds 2: %d", rr_ctf_held(&m, 1));
    CHECK(!m.teams[1].eliminated, "team 1 is not out, it still has flags");
    CHECK(m.winner == RR_CTF_NONE, "not won");
    CHECK(m.teams[1].point_flag[0] == -1, "the point is empty");
    CHECK((uint8_t)(m.event_seq - seq0) >= 2, "events were pushed (taken, banked)");
    steal_and_bank(0, 0, 1, 1);
    CHECK(rr_ctf_held(&m, 1) == 1, "team 1 holds 1");
    CHECK(!m.teams[1].eliminated, "one flag is enough to stay in");
    CHECK(m.winner == RR_CTF_NONE, "one to go");
    steal_and_bank(0, 0, 1, 2);
    CHECK(rr_ctf_held(&m, 1) == 0, "team 1 holds nothing");
    CHECK(m.teams[1].eliminated, "team 1 is out with no flags");
    CHECK(m.winner == 0, "team 0 won, winner %d", m.winner);
    CHECK(m.events[RR_CTF_EVENT_HISTORY - 1].type == rr_ctf_event_won, "won event last");
}

static void test_carry_follows_and_drop()
{
    start_match(2);
    place(0, 0, flag(1, 1)->x, flag(1, 1)->y);
    step(1);
    CHECK(flag(1, 1)->state == rr_ctf_flag_carried && flag(1, 1)->carrier == 0, "carried");
    CHECK(m.teams[1].point_flag[1] == -1, "point freed");
    CHECK(rr_ctf_held(&m, 1) == 3, "a carried flag still counts for its team");
    CHECK(!m.teams[1].eliminated, "not out while it is only carried");
    place(0, 0, 3000, 400);
    step(1);
    CHECK(flag(1, 1)->x == 3000 && flag(1, 1)->y == 400, "flag follows the carrier");
    // the carrier dies
    players[0].alive = 0;
    step(1);
    CHECK(flag(1, 1)->state == rr_ctf_flag_dropped, "dropped when the carrier dies");
    CHECK(flag(1, 1)->x == 3000 && flag(1, 1)->y == 400, "where it was");
    CHECK(flag(1, 1)->holder == 1, "still team 1's");
    // not back before the timer
    step(RR_CTF_RETURN_TICKS - 5);
    CHECK(flag(1, 1)->state == rr_ctf_flag_dropped, "not back yet");
    step(10);
    CHECK(flag(1, 1)->state == rr_ctf_flag_standing && flag(1, 1)->point == 1 &&
              m.teams[1].point_flag[1] == 1 * 3 + 1,
          "back on its point after the timer");
    CHECK(flag(1, 1)->x == px(1, 1), "at the point");
}

static void test_drops_where_the_carrier_died()
{
    start_match(2);
    place(0, 0, flag(1, 1)->x, flag(1, 1)->y);
    step(1);
    place(0, 0, 3000, 400);
    step(1);
    // the carrier moves and dies in the same tick, the flag falls where they died
    place(0, 0, 3217, 655);
    players[0].alive = 0;
    step(1);
    CHECK(flag(1, 1)->state == rr_ctf_flag_dropped, "dropped");
    CHECK(flag(1, 1)->x == 3217 && flag(1, 1)->y == 655,
          "where they died, not where they were a tick before: %.0f %.0f",
          flag(1, 1)->x, flag(1, 1)->y);
    // a carrier who left has no position, the flag stays where it was
    start_match(2);
    place(0, 0, flag(1, 1)->x, flag(1, 1)->y);
    step(1);
    place(0, 0, 4100, 300);
    step(1);
    players[0] = (struct rr_ctf_player){0};
    step(1);
    CHECK(flag(1, 1)->state == rr_ctf_flag_dropped, "dropped on leaving");
    CHECK(flag(1, 1)->x == 4100 && flag(1, 1)->y == 300,
          "a player who left drops it where it last was, not at the origin: %.0f %.0f",
          flag(1, 1)->x, flag(1, 1)->y);
}

static void test_drop_returned_by_owner()
{
    start_match(2);
    place(0, 0, flag(1, 2)->x, flag(1, 2)->y);
    step(1);
    place(0, 0, 4000, 0);
    step(1);
    players[0].present = 0; // disconnects while carrying
    step(1);
    CHECK(flag(1, 2)->state == rr_ctf_flag_dropped, "dropped on disconnect");
    // a player of the holder's team touches it: it goes home at once
    place(5, 1, 4000, 0);
    step(1);
    CHECK(flag(1, 2)->state == rr_ctf_flag_standing, "returned by its own team");
    // an enemy touches a dropped flag: takes it
    place(0, 0, flag(1, 2)->x, flag(1, 2)->y);
    step(1);
    players[0].alive = 0;
    step(1);
    CHECK(flag(1, 2)->state == rr_ctf_flag_dropped, "dropped again");
    place(1, 0, flag(1, 2)->x, flag(1, 2)->y);
    step(1);
    CHECK(flag(1, 2)->state == rr_ctf_flag_carried && flag(1, 2)->carrier == 1,
          "another enemy takes it from the ground");
}

static void test_one_flag_per_carrier()
{
    start_match(2);
    // standing between two flags of the same team: only one is taken
    rr_ctf_set_point(&m, 1, 0, 20000, 0);
    rr_ctf_set_point(&m, 1, 1, 20050, 0);
    rr_ctf_create_flags(&m);
    place(0, 0, 20025, 0);
    step(1);
    int carried = 0;
    for (int i = 0; i < 3; ++i)
        carried += flag(1, i)->state == rr_ctf_flag_carried;
    CHECK(carried == 1, "one flag per carrier, got %d", carried);
    // two players on one flag: only one gets it
    start_match(2);
    place(0, 0, flag(1, 0)->x, flag(1, 0)->y);
    place(1, 0, flag(1, 0)->x, flag(1, 0)->y);
    step(1);
    CHECK(flag(1, 0)->carrier == 0, "the first player gets it");
}

static void test_three_teams_setup_and_recover()
{
    start_match(3);
    // team 0 takes one of team 1's flags
    steal_and_bank(0, 0, 1, 0);
    CHECK(!m.teams[1].eliminated, "team 1 still holds %d", rr_ctf_held(&m, 1));
    CHECK(rr_ctf_held(&m, 0) == 4, "team 0 holds 4");
    // team 2 takes one of team 0's flags
    steal_and_bank(7, 2, 0, 0);
    CHECK(flag(0, 0)->holder == 2 && flag(0, 0)->state == rr_ctf_flag_banked, "banked by 2");
    CHECK(rr_ctf_held(&m, 0) == 3, "team 0 holds 3: %d", rr_ctf_held(&m, 0));
    CHECK(rr_ctf_held(&m, 2) == 4, "team 2 holds 4");
    CHECK(m.teams[0].point_flag[0] == -1, "team 0's point 0 is empty");
    CHECK(rr_ctf_banked(&m, 0) == 1, "team 0 has one banked");
    // team 0 sets the banked flag up on the empty point: 3 seconds on it
    place(0, 0, 5000, 5000);
    place(2, 0, px(0, 0), py(0, 0));
    step(RR_CTF_SETUP_TICKS - 2);
    CHECK(m.teams[0].point_flag[0] == -1, "not yet");
    step(1);
    CHECK(m.teams[0].point_flag[0] == -1, "not yet 2");
    step(2);
    CHECK(m.teams[0].point_flag[0] != -1, "set up after 3 seconds");
    {
        int8_t f = m.teams[0].point_flag[0];
        CHECK(m.flags[f].state == rr_ctf_flag_standing && m.flags[f].holder == 0,
              "standing for team 0");
        CHECK(m.flags[f].owner == 1, "it is team 1's flag, from the bank");
        CHECK(rr_ctf_held(&m, 0) == 3, "still holds 3");
        CHECK(rr_ctf_held_foreign(&m, 0) == 1, "1 foreign held");
    }
    CHECK(rr_ctf_banked(&m, 0) == 0, "bank is empty");
    // team 2 takes that flag off team 0's point, and it is theirs
    uint8_t f = m.teams[0].point_flag[0];
    place(7, 2, m.flags[f].x, m.flags[f].y);
    step(1);
    CHECK(m.flags[f].state == rr_ctf_flag_carried, "taken from the point");
    place(7, 2, px(2, 2), py(2, 2));
    step(1);
    CHECK(rr_ctf_held(&m, 0) == 2, "team 0 holds 2: %d", rr_ctf_held(&m, 0));
    CHECK(!m.teams[0].eliminated, "team 0 is still in");
    CHECK(rr_ctf_held(&m, 2) == 5, "team 2 holds 5: %d", rr_ctf_held(&m, 2));
    CHECK(m.winner == RR_CTF_NONE, "not won");
}

static void test_setup_needs_presence_and_decays()
{
    start_match(3);
    steal_and_bank(0, 0, 1, 0);  // team 1 out, team 0 holds 4
    steal_and_bank(7, 2, 0, 1);  // team 0 holds 3 (point 1 is empty), team 2 holds 4
    CHECK(rr_ctf_banked(&m, 0) == 1, "banked one from team 1");
    // the player who banked it is standing on team 0's point 1, send them away
    // so that only the player under test is on it
    place(0, 0, 5000, 5000);
    place(2, 0, px(0, 1), py(0, 1));
    step(RR_CTF_SETUP_TICKS - 5);
    CHECK(m.teams[0].setup_ticks[1] > 0, "progress");
    players[2].x = 5000; // wanders off
    step(RR_CTF_SETUP_TICKS);
    CHECK(m.teams[0].point_flag[1] == -1, "walking away loses the progress");
    CHECK(m.teams[0].setup_ticks[1] == 0, "progress is gone");
    // an enemy standing on the point does not help
    place(3, 2, px(0, 1), py(0, 1));
    players[2].x = 5000;
    step(RR_CTF_SETUP_TICKS + 5);
    CHECK(m.teams[0].point_flag[1] == -1, "only the team can set up its point");
    // a point with a flag standing on it can't be set up
    place(4, 0, px(0, 2), py(0, 2));
    step(RR_CTF_SETUP_TICKS + 5);
    CHECK(m.teams[0].point_flag[2] == 0 * 3 + 2, "an occupied point is left alone");
}

static void test_eliminated_team_gives_up_its_bank()
{
    start_match(3);
    // team 0 banks two of team 1's flags
    steal_and_bank(0, 0, 1, 0);
    steal_and_bank(0, 0, 1, 1);
    CHECK(rr_ctf_banked(&m, 0) == 2, "team 0 banked 2");
    // banked flags are safe, so taking team 0's own flags can't put it out
    steal_and_bank(7, 2, 0, 0);
    steal_and_bank(7, 2, 0, 1);
    steal_and_bank(7, 2, 0, 2);
    CHECK(rr_ctf_held(&m, 0) == 2, "team 0 holds only its bank: %d", rr_ctf_held(&m, 0));
    CHECK(!m.teams[0].eliminated, "a team with a banked flag stays in");
    // everyone of team 0 leaves: out, and the team that took its last flag gets the bank
    rr_ctf_eliminate(&m, 0, 2);
    CHECK(m.teams[0].eliminated, "team 0 out");
    CHECK(rr_ctf_banked(&m, 0) == 0, "its bank was handed over");
    CHECK(flag(1, 0)->holder == 2 && flag(1, 1)->holder == 2, "team 2 got team 1's flags");
    CHECK(rr_ctf_held(&m, 2) == 3 + 3 + 2, "team 2 holds 8: %d", rr_ctf_held(&m, 2));
    step(1);
    CHECK(m.winner == RR_CTF_NONE, "team 1's third flag and nothing of team 0's... is still out");
    steal_and_bank(7, 2, 1, 2);
    CHECK(rr_ctf_held_foreign(&m, 2) == 6, "team 2 holds all 6 of the others: %d",
          rr_ctf_held_foreign(&m, 2));
    CHECK(m.winner == 2, "team 2 won, winner %d", m.winner);
}

static void test_eliminated_players_do_nothing()
{
    start_match(2);
    rr_ctf_eliminate(&m, 1, 0);
    place(9, 1, flag(0, 0)->x, flag(0, 0)->y);
    step(5);
    CHECK(flag(0, 0)->state == rr_ctf_flag_standing, "an eliminated team can't take flags");
}

static void test_one_flag_is_enough()
{
    start_match(2);
    steal_and_bank(0, 0, 1, 0);
    steal_and_bank(0, 0, 1, 1);
    CHECK(rr_ctf_held(&m, 1) == 1 && !m.teams[1].eliminated, "one flag keeps a team in");
    // the team that is down to one flag still plays: it can take a flag of the
    // other team
    place(5, 1, flag(0, 0)->x, flag(0, 0)->y);
    step(1);
    CHECK(flag(0, 0)->state == rr_ctf_flag_carried && flag(0, 0)->carrier == 5,
          "a team with one flag still plays");
    players[5].alive = 0;
    step(1);
    steal_and_bank(0, 0, 1, 2);
    CHECK(m.teams[1].eliminated && m.winner == 0, "taking the last one ends it");
}

static void test_carrier_of_eliminated_team_drops()
{
    start_match(3);
    // team 1 carries a flag of team 2 and then team 1 falls
    place(1, 1, flag(2, 0)->x, flag(2, 0)->y);
    step(1);
    CHECK(flag(2, 0)->carrier == 1, "team 1 carries a flag of team 2");
    rr_ctf_eliminate(&m, 1, 0);
    step(1);
    CHECK(flag(2, 0)->state == rr_ctf_flag_dropped, "dropped when its team is out");
}

static void test_no_flags_before_the_end_of_the_placement()
{
    rr_ctf_match_reset(&m);
    m.team_count = 2;
    m.teams[0].active = m.teams[1].active = 1;
    memset(players, 0, sizeof players);
    place(0, 0, 0, 0);
    step(10);
    CHECK(!m.teams[0].eliminated, "rules do nothing before the flags exist");
    CHECK(!rr_ctf_all_points_set(&m), "points are not set");
    rr_ctf_set_point(&m, 0, 0, 1, 1);
    CHECK(!rr_ctf_all_points_set(&m), "still not");
}

int main()
{
    test_start();
    test_own_flags_are_safe();
    test_two_team_game();
    test_carry_follows_and_drop();
    test_drops_where_the_carrier_died();
    test_drop_returned_by_owner();
    test_one_flag_per_carrier();
    test_three_teams_setup_and_recover();
    test_setup_needs_presence_and_decays();
    test_eliminated_team_gives_up_its_bank();
    test_eliminated_players_do_nothing();
    test_one_flag_is_enough();
    test_carrier_of_eliminated_team_drops();
    test_no_flags_before_the_end_of_the_placement();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
