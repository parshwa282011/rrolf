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

// The event loop thread (libwebsockets) must never block on a database,
// argon2 or an outgoing HTTPS call. Anything slow is submitted here: `run`
// executes on a worker thread, then `done` executes on the event loop thread
// (from ms_jobs_drain_done) with the same argument.

struct lws_context;

typedef void (*ms_job_run)(void *arg);
typedef void (*ms_job_done)(void *arg);

void ms_jobs_init(int workers, struct lws_context *context);
void ms_jobs_set_context(struct lws_context *context);
// thread safe
void ms_jobs_submit(ms_job_run run, ms_job_done done, void *arg);
// event loop thread only; returns the number of completions processed
int ms_jobs_drain_done();
// waits for queued work to finish, then joins the workers
void ms_jobs_shutdown();
// number of jobs queued or running
int ms_jobs_pending();
