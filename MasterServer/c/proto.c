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

#include "proto.h"

#include <stdlib.h>
#include <string.h>

void ms_reader_init(struct ms_reader *this, uint8_t const *data, size_t len)
{
    this->data = data;
    this->len = len;
    this->at = 0;
    this->error = 0;
}

uint8_t ms_reader_u8(struct ms_reader *this)
{
    if (this->at >= this->len)
    {
        this->error = 1;
        return 0;
    }
    return this->data[this->at++];
}

uint64_t ms_reader_varuint(struct ms_reader *this)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    uint8_t byte;
    do
    {
        byte = ms_reader_u8(this);
        if (this->error)
            return 0;
        if (shift < 64)
            value |= (uint64_t)(byte >> 1) << shift;
        shift += 7;
        if (shift > 70)
        {
            this->error = 1;
            return 0;
        }
    } while (byte & 1);
    return value;
}

double ms_reader_f64(struct ms_reader *this)
{
    if (this->len - this->at < 8 || this->at > this->len)
    {
        this->error = 1;
        return 0;
    }
    double v;
    memcpy(&v, this->data + this->at, 8);
    this->at += 8;
    return v;
}

void ms_reader_string(struct ms_reader *this, char *out, size_t size)
{
    size_t i = 0;
    out[0] = 0;
    while (1)
    {
        uint8_t c = ms_reader_u8(this);
        if (this->error)
        {
            out[i < size ? i : size - 1] = 0;
            return;
        }
        if (c == 0)
        {
            out[i] = 0;
            return;
        }
        if (i + 1 >= size)
        {
            // keep consuming so the stream stays aligned, but flag it
            this->error = 1;
            out[size - 1] = 0;
            return;
        }
        out[i++] = (char)c;
    }
}

size_t ms_reader_remaining(struct ms_reader const *this)
{
    return this->at >= this->len ? 0 : this->len - this->at;
}

void ms_writer_init(struct ms_writer *this, size_t cap, int success_byte)
{
    this->data = malloc(cap);
    this->cap = this->data ? cap : 0;
    this->at = 0;
    this->error = this->data == NULL;
    if (success_byte)
        ms_writer_u8(this, MS_API_SUCCESS);
}

void ms_writer_free(struct ms_writer *this)
{
    free(this->data);
    this->data = NULL;
    this->cap = this->at = 0;
}

void ms_writer_u8(struct ms_writer *this, uint8_t v)
{
    if (this->error || this->at >= this->cap)
    {
        this->error = 1;
        return;
    }
    this->data[this->at++] = v;
}

void ms_writer_varuint(struct ms_writer *this, uint64_t v)
{
    while (v > 127)
    {
        ms_writer_u8(this, (uint8_t)((v & 127) << 1 | 1));
        v >>= 7;
    }
    ms_writer_u8(this, (uint8_t)(v << 1));
}

void ms_writer_f64(struct ms_writer *this, double v)
{
    if (this->error || this->cap - this->at < 8)
    {
        this->error = 1;
        return;
    }
    memcpy(this->data + this->at, &v, 8);
    this->at += 8;
}

void ms_writer_string(struct ms_writer *this, char const *s)
{
    size_t len = strlen(s);
    if (this->error || this->cap - this->at < len + 1)
    {
        this->error = 1;
        return;
    }
    memcpy(this->data + this->at, s, len);
    this->at += len;
    this->data[this->at++] = 0;
}
