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

// What the Garden's server and the client agree on about the snail run, see
// Server/Snail.h for what it is.

enum rr_snail_phase
{
    rr_snail_phase_idle,    // waiting for the next run
    rr_snail_phase_running, // the snail is on its way
    rr_snail_phase_arrived, // shown for a few seconds, then idle again
    rr_snail_phase_failed
};

enum rr_snail_event_type
{
    rr_snail_event_none,
    rr_snail_event_started, // rarity
    rr_snail_event_arrived, // rarity, how many players were rewarded
    rr_snail_event_eaten,
    rr_snail_event_lost // nobody was around for too long
};

#define RR_SNAIL_EVENT_HISTORY 4

// How many snails can be going at once. One is the timed event, the others are
// started from the dev panel
#define RR_SNAIL_MAX_RUNS 101
