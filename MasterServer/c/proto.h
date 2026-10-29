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

#include <stddef.h>
#include <stdint.h>

// Wire format compatible with MasterServer/protocol.js and
// Shared/Binary.c (rr_binary_encoder): little endian floats, NUL terminated
// strings, varuint = 7 bits per byte stored in bits 1..7, bit 0 = "more".

#define MS_API_SUCCESS 45

struct ms_reader
{
    uint8_t const *data;
    size_t len;
    size_t at;
    int error; // set when a read ran past the end or a string was unterminated
};

void ms_reader_init(struct ms_reader *, uint8_t const *, size_t);
uint8_t ms_reader_u8(struct ms_reader *);
uint64_t ms_reader_varuint(struct ms_reader *);
double ms_reader_f64(struct ms_reader *);
// copies at most size - 1 bytes; flags an error if the string is longer
void ms_reader_string(struct ms_reader *, char *out, size_t size);
size_t ms_reader_remaining(struct ms_reader const *);

struct ms_writer
{
    uint8_t *data;
    size_t cap;
    size_t at;
    int error; // set on overflow; further writes are ignored
};

// Allocates its own buffer. The first byte is MS_API_SUCCESS when
// `success_byte` is non-zero (master -> game server messages).
void ms_writer_init(struct ms_writer *, size_t cap, int success_byte);
void ms_writer_free(struct ms_writer *);
void ms_writer_u8(struct ms_writer *, uint8_t);
void ms_writer_varuint(struct ms_writer *, uint64_t);
void ms_writer_f64(struct ms_writer *, double);
void ms_writer_string(struct ms_writer *, char const *);
