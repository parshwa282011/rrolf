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

#include "server.h"

#include <ctype.h>
#include <libwebsockets.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "games.h"
#include "jobs.h"
#include "util.h"

#define MAX_BODY (32 * 1024)

static volatile sig_atomic_t quit;
static struct lws_context *context;
static lws_sorted_usec_list_t tick_sul;

// One HTTP request. Allocated on the event loop thread, handled by a worker.
struct http_job
{
    struct lws *wsi; // event loop thread only, NULL once the client went away
    struct ms_req req;
    struct ms_res res;
    uint8_t *out; // LWS_PRE padded body
    int queued;
    int finished; // a worker is no longer touching it
    int ready;    // response complete, waiting to be written
    int stage;    // 0 headers, 1 body
    int rejected; // body too large
};

struct http_pss
{
    struct http_job *job;
};

static void job_free(struct http_job *job)
{
    free(job->req.body);
    ms_res_free(&job->res);
    free(job->out);
    free(job);
}

static void run_route(void *arg)
{
    struct http_job *job = arg;
    ms_route(&job->req, &job->res);
}

static void route_done(void *arg)
{
    struct http_job *job = arg;
    job->finished = 1;
    if (job->wsi == NULL)
    {
        job_free(job); // the client left while we were working
        return;
    }
    job->ready = 1;
    lws_callback_on_writable(job->wsi);
}

static void dispatch(struct http_job *job)
{
    if (job->rejected)
    {
        ms_res_error(&job->res, 413, "too_large", NULL);
        job->ready = 1;
        job->finished = 1;
        lws_callback_on_writable(job->wsi);
        return;
    }
    job->queued = 1;
    ms_jobs_submit(run_route, route_done, job);
}

static int is_loopback(char const *ip)
{
    return strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "::1") == 0 ||
           strcmp(ip, "::ffff:127.0.0.1") == 0;
}

static void fill_request(struct lws *wsi, struct http_job *job,
                         char const *path)
{
    struct ms_req *req = &job->req;
    ms_strlcpy(req->path, path, sizeof req->path);
    // strip a trailing slash except for the root
    size_t len = strlen(req->path);
    if (len > 1 && req->path[len - 1] == '/')
        req->path[len - 1] = 0;
    lws_hdr_copy(wsi, req->query, sizeof req->query, WSI_TOKEN_HTTP_URI_ARGS);
    lws_hdr_copy(wsi, req->origin, sizeof req->origin, WSI_TOKEN_ORIGIN);
    lws_hdr_custom_copy(wsi, req->server_secret, sizeof req->server_secret,
                        "x-server-secret:", 16);

    char peer[64] = "";
    lws_get_peer_simple(wsi, peer, sizeof peer);
    ms_strlcpy(req->ip, peer, sizeof req->ip);
    // only believe the proxy header when the request came from this machine
    // (cloudflared connects over loopback)
    if (MS_CONFIG.trust_proxy_header[0] && is_loopback(peer))
    {
        char name[96];
        size_t n = 0;
        for (char const *c = MS_CONFIG.trust_proxy_header; *c && n < 90; ++c)
            name[n++] = (char)tolower((unsigned char)*c);
        name[n++] = ':';
        name[n] = 0;
        char value[64] = "";
        if (lws_hdr_custom_copy(wsi, value, sizeof value, name, (int)n) > 0)
            ms_strlcpy(req->ip, value, sizeof req->ip);
    }
}

static int write_headers(struct lws *wsi, struct http_job *job)
{
    struct ms_res *res = &job->res;
    uint8_t buf[LWS_PRE + 2048];
    uint8_t *start = buf + LWS_PRE;
    uint8_t *p = start;
    uint8_t *end = buf + sizeof buf - 1;

    if (lws_add_http_header_status(wsi, (unsigned)res->status, &p, end))
        return -1;
    if (res->body_len > 0 &&
        lws_add_http_header_by_token(
            wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
            (uint8_t const *)res->content_type,
            (int)strlen(res->content_type), &p, end))
        return -1;
    if (lws_add_http_header_content_length(wsi, res->body_len, &p, end))
        return -1;
    if (lws_add_http_header_by_name(wsi, (uint8_t const *)"cache-control:",
                                    (uint8_t const *)"no-store", 8, &p, end))
        return -1;
    if (lws_add_http_header_by_name(wsi, (uint8_t const *)"x-content-type-options:",
                                    (uint8_t const *)"nosniff", 7, &p, end))
        return -1;
    if (res->location[0] &&
        lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_LOCATION,
                                     (uint8_t const *)res->location,
                                     (int)strlen(res->location), &p, end))
        return -1;
    if (res->allow_origin[0])
    {
        struct
        {
            char const *name;
            char const *value;
        } cors[] = {
            {"access-control-allow-origin:", res->allow_origin},
            {"vary:", "Origin"},
            {"access-control-allow-methods:", "GET, POST, OPTIONS"},
            {"access-control-allow-headers:", "content-type"},
            {"access-control-max-age:", "600"},
        };
        for (size_t i = 0; i < sizeof cors / sizeof *cors; ++i)
            if (lws_add_http_header_by_name(wsi, (uint8_t const *)cors[i].name,
                                            (uint8_t const *)cors[i].value,
                                            (int)strlen(cors[i].value), &p, end))
                return -1;
    }
    if (lws_finalize_write_http_header(wsi, start, &p, end))
        return -1;
    return 0;
}

static int http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    struct http_pss *pss = user;
    switch (reason)
    {
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        // a worker finished something, or a signal asked us to stop
        ms_jobs_drain_done();
        ms_games_tick();
        return 0;

    case LWS_CALLBACK_HTTP:
    {
        struct http_job *job = calloc(1, sizeof *job);
        job->wsi = wsi;
        pss->job = job;
        int is_get = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0;
        int is_post = lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0;
        int is_options = lws_hdr_total_length(wsi, WSI_TOKEN_OPTIONS_URI) > 0;
        ms_strlcpy(job->req.method,
                   is_get ? "GET" : is_post ? "POST" : is_options ? "OPTIONS" : "?",
                   sizeof job->req.method);
        fill_request(wsi, job, in ? (char const *)in : "/");
        if (!is_get && !is_post && !is_options)
        {
            ms_res_error(&job->res, 405, "method_not_allowed", NULL);
            job->ready = job->finished = 1;
            lws_callback_on_writable(wsi);
            return 0;
        }
        if (is_post)
        {
            // the body follows in LWS_CALLBACK_HTTP_BODY
            char cl[16] = "";
            lws_hdr_copy(wsi, cl, sizeof cl, WSI_TOKEN_HTTP_CONTENT_LENGTH);
            if (atol(cl) > MAX_BODY)
                job->rejected = 1;
            return 0;
        }
        dispatch(job);
        return 0;
    }

    case LWS_CALLBACK_HTTP_BODY:
    {
        struct http_job *job = pss->job;
        if (job == NULL || job->rejected)
            return 0;
        if (job->req.body_len + len > MAX_BODY)
        {
            job->rejected = 1;
            return 0;
        }
        job->req.body = realloc(job->req.body, job->req.body_len + len + 1);
        memcpy(job->req.body + job->req.body_len, in, len);
        job->req.body_len += len;
        job->req.body[job->req.body_len] = 0;
        return 0;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
        if (pss->job != NULL)
            dispatch(pss->job);
        return 0;

    case LWS_CALLBACK_HTTP_WRITEABLE:
    {
        struct http_job *job = pss->job;
        if (job == NULL || !job->ready)
            return 0;
        if (job->stage == 0)
        {
            if (job->res.status == 0)
                job->res.status = 500;
            if (write_headers(wsi, job))
                return -1;
            if (job->res.body_len > 0)
            {
                job->stage = 1;
                lws_callback_on_writable(wsi);
                return 0;
            }
        }
        else
        {
            job->out = malloc(LWS_PRE + job->res.body_len);
            memcpy(job->out + LWS_PRE, job->res.body, job->res.body_len);
            if (lws_write(wsi, job->out + LWS_PRE, job->res.body_len,
                          LWS_WRITE_HTTP_FINAL) < 0)
                return -1;
        }
        // response complete; the connection may carry another request
        job_free(job);
        pss->job = NULL;
        if (lws_http_transaction_completed(wsi))
            return -1;
        return 0;
    }

    case LWS_CALLBACK_CLOSED_HTTP:
    {
        struct http_job *job = pss ? pss->job : NULL;
        if (job != NULL)
        {
            job->wsi = NULL;
            if (!job->queued || job->finished)
                job_free(job);
            // otherwise route_done frees it
            pss->job = NULL;
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void tick(lws_sorted_usec_list_t *sul)
{
    ms_jobs_drain_done();
    ms_games_tick();
    lws_sul_schedule(context, 0, &tick_sul, tick, 250 * LWS_US_PER_MS);
}

void ms_server_stop()
{
    quit = 1;
    if (context != NULL)
        lws_cancel_service(context);
}

int ms_server_run()
{
    static lws_retry_bo_t const retry = {
        .secs_since_valid_ping = 30,
        .secs_since_valid_hangup = 120,
    };
    static struct lws_protocols protocols[3];
    protocols[0] = (struct lws_protocols){"http", http_callback,
                                          sizeof(struct http_pss), 4096, 0,
                                          NULL, 0};
    protocols[1] = (struct lws_protocols){"g", ms_games_ws_callback,
                                          ms_games_session_size(), 16384, 0,
                                          NULL, 0};
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof info);
    info.port = MS_CONFIG.port;
    info.iface = MS_CONFIG.bind_address[0] ? MS_CONFIG.bind_address : NULL;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8 |
                   LWS_SERVER_OPTION_DISABLE_IPV6;
    info.retry_and_idle_policy = &retry;
    info.timeout_secs = 30;
    info.pt_serv_buf_size = 32 * 1024;

    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
    context = lws_create_context(&info);
    if (context == NULL)
    {
        ms_log("server", "could not listen on port %u", MS_CONFIG.port);
        return -1;
    }
    ms_jobs_set_context(context);
    ms_games_set_context(context);
    lws_sul_schedule(context, 0, &tick_sul, tick, 250 * LWS_US_PER_MS);
    ms_log("server", "listening on %s:%u (db: %s)",
           MS_CONFIG.bind_address[0] ? MS_CONFIG.bind_address : "*",
           MS_CONFIG.port, MS_DB->name);

    while (!quit)
        if (lws_service(context, 0) < 0)
            break;

    ms_log("server", "stopping");
    // detach workers from the context before it goes away
    ms_jobs_set_context(NULL);
    ms_games_set_context(NULL);
    struct lws_context *c = context;
    context = NULL;
    lws_sul_cancel(&tick_sul);
    lws_context_destroy(c); // closes every websocket: players get flushed
    ms_jobs_shutdown();
    ms_games_shutdown();
    return 0;
}
