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

#include <stdint.h>

// Fixed window counters keyed by an arbitrary string (e.g. "login:ip:1.2.3.4").
// Thread safe. Memory is bounded: old windows are recycled, and when the table
// is saturated the least recently used slot is reused.

// Counts one event. Returns 1 if it is within `limit` per `window` seconds,
// 0 if the caller should be rejected.
int ms_rl_hit(char const *key, uint32_t limit, uint32_t window);

// Returns 1 if `limit` events were already counted in the current window
// (does not count anything).
int ms_rl_blocked(char const *key, uint32_t limit, uint32_t window);

// Counts one event without checking (used for failed attempts).
void ms_rl_add(char const *key, uint32_t window);

void ms_rl_reset(char const *key);
