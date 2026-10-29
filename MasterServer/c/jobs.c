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

#include "jobs.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <stdlib.h>

#include "util.h"

struct job
{
    struct job *next;
    ms_job_run run;
    ms_job_done done;
    void *arg;
};

#define MAX_WORKERS 32

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static struct job *todo_head, *todo_tail;
static struct job *done_head, *done_tail;
static pthread_t workers[MAX_WORKERS];
static int worker_count;
static int stopping;
static int pending;
static struct lws_context *context;

static void push(struct job **head, struct job **tail, struct job *j)
{
    j->next = NULL;
    if (*tail)
        (*tail)->next = j;
    else
        *head = j;
    *tail = j;
}

static struct job *pop(struct job **head, struct job **tail)
{
    struct job *j = *head;
    if (j)
    {
        *head = j->next;
        if (*head == NULL)
            *tail = NULL;
    }
    return j;
}

static void *worker_main(void *unused)
{
    while (1)
    {
        pthread_mutex_lock(&lock);
        while (todo_head == NULL && !stopping)
            pthread_cond_wait(&wake, &lock);
        struct job *j = pop(&todo_head, &todo_tail);
        pthread_mutex_unlock(&lock);
        if (j == NULL)
            return NULL; // stopping and nothing left

        j->run(j->arg);

        pthread_mutex_lock(&lock);
        push(&done_head, &done_tail, j);
        struct lws_context *c = context;
        pthread_mutex_unlock(&lock);
        if (c != NULL)
            lws_cancel_service(c);
    }
}

void ms_jobs_init(int count, struct lws_context *ctx)
{
    if (count < 1)
        count = 1;
    if (count > MAX_WORKERS)
        count = MAX_WORKERS;
    context = ctx;
    for (int i = 0; i < count; ++i)
        if (pthread_create(&workers[worker_count], NULL, worker_main, NULL) == 0)
            ++worker_count;
    ms_log("jobs", "%d worker threads", worker_count);
}

void ms_jobs_set_context(struct lws_context *ctx)
{
    pthread_mutex_lock(&lock);
    context = ctx;
    pthread_mutex_unlock(&lock);
}

void ms_jobs_submit(ms_job_run run, ms_job_done done, void *arg)
{
    struct job *j = malloc(sizeof *j);
    j->run = run;
    j->done = done;
    j->arg = arg;
    pthread_mutex_lock(&lock);
    ++pending;
    push(&todo_head, &todo_tail, j);
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
}

int ms_jobs_drain_done()
{
    int n = 0;
    while (1)
    {
        pthread_mutex_lock(&lock);
        struct job *j = pop(&done_head, &done_tail);
        pthread_mutex_unlock(&lock);
        if (j == NULL)
            break;
        if (j->done != NULL)
            j->done(j->arg);
        pthread_mutex_lock(&lock);
        --pending;
        pthread_mutex_unlock(&lock);
        free(j);
        ++n;
    }
    return n;
}

int ms_jobs_pending()
{
    pthread_mutex_lock(&lock);
    int n = pending;
    pthread_mutex_unlock(&lock);
    return n;
}

void ms_jobs_shutdown()
{
    pthread_mutex_lock(&lock);
    stopping = 1;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    for (int i = 0; i < worker_count; ++i)
        pthread_join(workers[i], NULL);
    worker_count = 0;
    ms_jobs_drain_done();
}
