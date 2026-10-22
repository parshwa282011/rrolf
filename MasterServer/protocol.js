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

const MAX_COUNT = BigInt(Number.MAX_SAFE_INTEGER);

class BinaryWriter
{
    constructor() {
        const buf = new ArrayBuffer(8);
        this.conv_u8 = new Uint8Array(buf);
        this.conv_f32 = new Float32Array(buf);
        this.conv_f64 = new Float64Array(buf);
        this.data = new Uint8Array(256 * 1024); //files can't be more than 256kb and binary is more memory efficient so this is a good cap
        this.at = 1;
        this.data[0] = 45; //success byte
    }
    WriteUint8(u8) {
        this.data[this.at++] = u8;
    }
    // see BinaryReader.ReadCount, a missing or bad value is written as 0
    WriteCount(count) {
        this.WriteVarUint64(Number.isFinite(count) && count > 0
            ? Math.min(Math.floor(count), Number.MAX_SAFE_INTEGER) : 0);
    }
    WriteVarUint64(vu) {
        vu = BigInt(vu);

        if (vu < 0n || vu > 0xFFFFFFFFFFFFFFFFn)
            throw new RangeError("VarUint64 out of range");

        while (vu > 127n) {
            this.WriteUint8(Number(((vu & 127n) << 1n) | 1n));
            vu >>= 7n;
        }

        this.WriteUint8(Number(vu << 1n));
    }
    WriteVarUint(vu) {
        while (vu > 127)
        {
            this.WriteUint8((vu << 1) | 1);
            vu >>= 7;
        }
        this.WriteUint8(vu << 1);
    }
    WriteStringNT(str) {
        const buf = new TextEncoder().encode(str);
        this.data.set(buf, this.at);
        this.at += buf.length;
        this.WriteUint8(0);
    }
    WriteFloat32(f32) {
        this.conv_f32[0] = f32;
        this.data.set(this.conv_u8.subarray(0,4), this.at);
        this.at += 4;
    }
    WriteFloat64(f64) {
        this.conv_f64[0] = f64;
        this.data.set(this.conv_u8, this.at);
        this.at += 8;
    }
}

class BinaryReader
{
    constructor(data) {
        const buf = new ArrayBuffer(8);
        this.conv_u8 = new Uint8Array(buf);
        this.conv_f32 = new Float32Array(buf);
        this.conv_f64 = new Float64Array(buf);
        this.data = data;
        this.at = 0;
    }
    ReadUint8() {
        return this.data[this.at++];
    }
    ReadVarUint64() {
        let byte, data = 0n, shift = 0n;

        do {
            byte = this.ReadUint8();
            data |= BigInt((byte & 254) >> 1) << shift;
            shift += 7n;
        } while (byte & 1);

        return data;
    }
    // A petal count or score: 64 bits on the wire, kept as a number, which is
    // exact up to MAX_SAFE_INTEGER (about 9e15). Anything bigger is clamped.
    ReadCount() {
        const count = this.ReadVarUint64();
        return count > MAX_COUNT ? Number(MAX_COUNT) : Number(count);
    }
    ReadVarUint() {
        let byte, data = 0, shift = 0;

        do
        {
            byte = this.ReadUint8();
            data |= ((byte & 254) << shift) >> 1;
            shift += 7;
        } while (byte & 1);

        return data;
    }
    ReadStringNT() {
        const start = this.at;
        while (this.ReadUint8());
        return new TextDecoder().decode(this.data.subarray(start, this.at-1));
    }
    ReadFloat32() {
        this.conv_u8.set(this.data.subarray(this.at, this.at += 4));
        return this.conv_f32[0];
    }
    ReadFloat64() {
        this.conv_u8.set(this.data.subarray(this.at, this.at += 8));
        return this.conv_f64[0];
    }
}

module.exports = { BinaryReader, BinaryWriter };
