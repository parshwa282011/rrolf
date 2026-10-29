// Backend conformance test. Runs the same checks against whichever backend
// DB_TYPE selects (POSTGRES_URI / MONGO_URI from the environment). It creates
// uniquely named accounts and leaves them behind; use a throwaway database.

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "db.h"
#include "util.h"

static int failures = 0;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void make_account(struct ms_db_account *a, char const *name,
                         char const *email)
{
    memset(a, 0, sizeof *a);
    ms_uuid_generate(a->uuid);
    ms_strlcpy(a->username, name, sizeof a->username);
    ms_strlcpy(a->email, email, sizeof a->email);
    ms_strlcpy(a->pw_hash, "$argon2id$v=19$m=1,t=1,p=1$salt$hash",
               sizeof a->pw_hash);
    randombytes_buf(a->token_key, sizeof a->token_key);
    a->created_at = ms_now();
}

int main()
{
    if (sodium_init() < 0)
        return 2;
    if (ms_config_load(".env.test") != 0)
        return 2;
    struct ms_db *db = ms_db_open();
    if (db == NULL)
    {
        printf("could not open database\n");
        return 2;
    }
    printf("backend: %s\n", db->name);
    CHECK(db->ping(db) == MS_DB_OK);

    char suffix[16];
    snprintf(suffix, sizeof suffix, "%u", randombytes_uniform(1000000));
    char name1[80], name2[80], email1[120], email2[120];
    snprintf(name1, sizeof name1, "Tester_%s", suffix);
    snprintf(name2, sizeof name2, "Other_%s", suffix);
    snprintf(email1, sizeof email1, "Tester_%s@Example.com", suffix);
    snprintf(email2, sizeof email2, "other_%s@example.com", suffix);

    struct ms_db_account a, b, got;
    make_account(&a, name1, email1);
    make_account(&b, name2, email2);
    CHECK(db->account_create(db, &a) == MS_DB_OK);
    CHECK(db->account_create(db, &b) == MS_DB_OK);

    // duplicates, case-insensitively
    struct ms_db_account dup;
    char upper[80];
    for (size_t i = 0; name1[i]; ++i)
        upper[i] = (char)(name1[i] >= 'a' && name1[i] <= 'z' ? name1[i] - 32
                                                             : name1[i]);
    upper[strlen(name1)] = 0;
    make_account(&dup, upper, "different@example.com");
    CHECK(db->account_create(db, &dup) == MS_DB_CONFLICT_USERNAME);
    make_account(&dup, "UniqueName", email1);
    ms_str_lower(dup.username, "uniquename_" , sizeof dup.username);
    strcat(dup.username, suffix);
    CHECK(db->account_create(db, &dup) == MS_DB_CONFLICT_EMAIL);

    // accounts without email never conflict on email
    struct ms_db_account noemail1, noemail2;
    char n3[80], n4[80];
    snprintf(n3, sizeof n3, "Social1_%s", suffix);
    snprintf(n4, sizeof n4, "Social2_%s", suffix);
    make_account(&noemail1, n3, "");
    make_account(&noemail2, n4, "");
    CHECK(db->account_create(db, &noemail1) == MS_DB_OK);
    CHECK(db->account_create(db, &noemail2) == MS_DB_OK);

    // lookups
    CHECK(db->account_get(db, ms_db_by_uuid, a.uuid, NULL, &got) == MS_DB_OK);
    CHECK(strcmp(got.username, name1) == 0);
    CHECK(strcmp(got.email, email1) == 0);
    CHECK(memcmp(got.token_key, a.token_key, 32) == 0);
    CHECK(strcmp(got.pw_hash, a.pw_hash) == 0);
    CHECK(!got.is_dev);
    char lower[80];
    ms_str_lower(lower, name1, sizeof lower);
    CHECK(db->account_get(db, ms_db_by_username, lower, NULL, &got) ==
          MS_DB_OK);
    CHECK(strcmp(got.uuid, a.uuid) == 0);
    ms_str_lower(lower, email1, sizeof lower);
    CHECK(db->account_get(db, ms_db_by_email, lower, NULL, &got) == MS_DB_OK);
    CHECK(strcmp(got.uuid, a.uuid) == 0);
    CHECK(db->account_get(db, ms_db_by_email, "", NULL, &got) ==
          MS_DB_NOT_FOUND);
    CHECK(db->account_get(db, ms_db_by_username, "nobody_here", NULL, &got) ==
          MS_DB_NOT_FOUND);

    // credentials + login time
    uint8_t new_key[32];
    randombytes_buf(new_key, 32);
    CHECK(db->account_set_credentials(db, a.uuid, "$argon2id$new", new_key) ==
          MS_DB_OK);
    CHECK(db->account_set_last_login(db, a.uuid, 1234567) == MS_DB_OK);
    CHECK(db->account_get(db, ms_db_by_uuid, a.uuid, NULL, &got) == MS_DB_OK);
    CHECK(strcmp(got.pw_hash, "$argon2id$new") == 0);
    CHECK(memcmp(got.token_key, new_key, 32) == 0);
    CHECK(got.last_login_at == 1234567);

    // links
    struct ms_db_link link;
    memset(&link, 0, sizeof link);
    strcpy(link.provider, "discord");
    snprintf(link.provider_user_id, sizeof link.provider_user_id, "d%s", suffix);
    strcpy(link.provider_email, "discord@example.com");
    strcpy(link.display_name, "Disc#1");
    link.linked_at = ms_now();
    CHECK(db->link_put(db, a.uuid, &link) == MS_DB_OK);
    CHECK(db->link_put(db, a.uuid, &link) == MS_DB_OK); // idempotent
    CHECK(db->link_put(db, b.uuid, &link) == MS_DB_CONFLICT_LINK);
    CHECK(db->account_get(db, ms_db_by_link, "discord", link.provider_user_id,
                          &got) == MS_DB_OK);
    CHECK(strcmp(got.uuid, a.uuid) == 0);
    struct ms_db_link links[4];
    CHECK(db->links_get(db, a.uuid, links, 4) == 1);
    CHECK(strcmp(links[0].provider, "discord") == 0);
    CHECK(strcmp(links[0].provider_email, "discord@example.com") == 0);
    CHECK(db->links_get(db, b.uuid, links, 4) == 0);
    CHECK(db->link_delete(db, a.uuid, "discord") == MS_DB_OK);
    CHECK(db->account_get(db, ms_db_by_link, "discord", link.provider_user_id,
                          &got) == MS_DB_NOT_FOUND);
    CHECK(db->link_put(db, b.uuid, &link) == MS_DB_OK); // free again

    // rysteria
    struct ms_db_rysteria r;
    ms_db_rysteria_init(&r);
    CHECK(db->rysteria_get(db, a.uuid, &r) == MS_DB_NOT_FOUND);
    r.xp = 1.5e22;
    r.checkpoint = 0x020103;
    r.pvp_points = 5000000000LL;
    free(r.petals);
    r.petals = strdup("{\"1:0\":5,\"27:6\":123456}");
    free(r.failed_crafts);
    r.failed_crafts = strdup("{\"3:1\":4}");
    free(r.mob_kills);
    r.mob_kills = strdup("{\"1:0\":9}");
    free(r.loadout);
    r.loadout = strdup("{\"1\":[2,3],\"2\":[4,5]}");
    strcpy(r.nickname, "Nick");
    CHECK(db->rysteria_put(db, a.uuid, &r) == MS_DB_OK);
    r.xp = 2.5e22;
    CHECK(db->rysteria_put(db, a.uuid, &r) == MS_DB_OK); // upsert
    ms_db_rysteria_free(&r);
    ms_db_rysteria_init(&r);
    CHECK(db->rysteria_get(db, a.uuid, &r) == MS_DB_OK);
    CHECK(r.xp == 2.5e22);
    CHECK(r.checkpoint == 0x020103);
    CHECK(r.pvp_points == 5000000000LL);
    CHECK(strstr(r.petals, "123456") != NULL);
    CHECK(strstr(r.failed_crafts, "\"3:1\"") != NULL);
    CHECK(strstr(r.mob_kills, "\"1:0\"") != NULL);
    CHECK(strstr(r.loadout, "\"2\"") != NULL);
    CHECK(strcmp(r.nickname, "Nick") == 0);
    ms_db_rysteria_free(&r);

    // gardn saves: add, idempotent re-add, remove, limit
    struct ms_db_gsave s[3];
    memset(s, 0, sizeof s);
    strcpy(s[0].save_id, "aaaa");
    s[0].score = 1000;
    s[0].loadout = strdup("[1,2,3]");
    s[0].created_at = 100;
    strcpy(s[1].save_id, "bbbb");
    s[1].score = 2000;
    s[1].loadout = strdup("[4,5,6]");
    s[1].created_at = 200;
    CHECK(db->gardn_apply(db, a.uuid, s, 2, NULL, 0) == MS_DB_OK);
    CHECK(db->gardn_apply(db, a.uuid, s, 2, NULL, 0) == MS_DB_OK);
    struct ms_db_gsave listed[MS_GARDN_MAX_SAVES + 5];
    int n = db->gardn_list(db, a.uuid, listed, MS_GARDN_MAX_SAVES + 5);
    CHECK(n == 2);
    if (n == 2)
    {
        CHECK(strcmp(listed[0].save_id, "bbbb") == 0); // newest first
        CHECK(listed[0].score == 2000);
        CHECK(strstr(listed[0].loadout, "4") != NULL);
        CHECK(listed[1].created_at == 100);
    }
    ms_db_gsave_free(listed, n);
    char removes[1][40] = {"aaaa"};
    CHECK(db->gardn_apply(db, a.uuid, NULL, 0, removes, 1) == MS_DB_OK);
    n = db->gardn_list(db, a.uuid, listed, 10);
    CHECK(n == 1);
    ms_db_gsave_free(listed, n);
    ms_db_gsave_free(s, 3);

    // trimming beyond the cap keeps the newest saves
    struct ms_db_gsave many[MS_GARDN_MAX_SAVES + 10];
    memset(many, 0, sizeof many);
    for (int i = 0; i < MS_GARDN_MAX_SAVES + 10; ++i)
    {
        snprintf(many[i].save_id, sizeof many[i].save_id, "m%04d", i);
        many[i].score = (uint32_t)i;
        many[i].loadout = strdup("[1]");
        many[i].created_at = 1000 + i;
    }
    CHECK(db->gardn_apply(db, b.uuid, many, MS_GARDN_MAX_SAVES + 10, NULL, 0) ==
          MS_DB_OK);
    n = db->gardn_list(db, b.uuid, listed, MS_GARDN_MAX_SAVES + 5);
    CHECK(n == MS_GARDN_MAX_SAVES);
    if (n == MS_GARDN_MAX_SAVES)
        CHECK(listed[0].score == MS_GARDN_MAX_SAVES + 9);
    ms_db_gsave_free(listed, n);
    ms_db_gsave_free(many, MS_GARDN_MAX_SAVES + 10);

    // leaderboards (generic placeholder)
    char board[40];
    snprintf(board, sizeof board, "test_%s", suffix);
    CHECK(db->lb_submit(db, board, a.uuid, 10, "{\"k\":1}") == MS_DB_OK);
    CHECK(db->lb_submit(db, board, b.uuid, 30, "{}") == MS_DB_OK);
    CHECK(db->lb_submit(db, board, a.uuid, 5, "{}") == MS_DB_OK); // lower: kept
    struct ms_db_lb_entry top[5];
    n = db->lb_top(db, board, 5, top);
    CHECK(n == 2);
    if (n == 2)
    {
        CHECK(top[0].score == 30);
        CHECK(strcmp(top[0].uuid, b.uuid) == 0);
        CHECK(top[1].score == 10);
    }
    ms_db_lb_free(top, n);

    ms_db_close(db);
    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all %s backend checks passed\n", "database");
    return 0;
}
