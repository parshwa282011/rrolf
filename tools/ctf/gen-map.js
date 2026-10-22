#!/usr/bin/env node
// Builds the capture the flag map from one hand-made quadrant.
//
//   node tools/ctf/gen-map.js            writes Shared/CtfMap.inc
//   node tools/ctf/gen-map.js --preview  also prints the whole map as text
//   node tools/ctf/gen-map.js --check    only checks the result, writes nothing
//
// tools/ctf/base-quadrant.txt is a 40x40 map template, the same letters as the
// other maps in Shared/StaticData.c (_ is a wall, the letters are difficulty).
// Its right column and bottom row are walls. The quadrant is mirrored
// horizontally and vertically into four, so those frames meet in the middle,
// and the duplicate is dropped so the seam between two team areas is exactly one
// wall cell thick:
//
//       T            | flipH(T)        columns 0..38 | 39 = seam | 40..78
//     --------------- seam row 39 ---------------
//       flipV(T)     | flipHV(T)
//
// That makes a 79x79 template (RR_DEFINE_MAZE(CTF, 158)). The seam cells are the
// only walls that drop after 10 minutes, see rr_ctf_drop_walls in Shared/Ctf.c.

const fs = require("fs");
const path = require("path");

const LETTERS = {
    _: 0, c: 1, C: 4, u: 8, U: 12, r: 16, R: 20, e: 24, E: 28,
    l: 32, L: 36, m: 40, M: 44, x: 48, X: 52, a: 56, A: 60,
};
const NAMES = Object.fromEntries(Object.entries(LETTERS).map(([k, v]) => [v, k]));

const Q = 40;            // the base quadrant is Q x Q
const DIM = 2 * Q - 1;   // 79, the seam is column and row Q - 1
const SEAM = Q - 1;      // 39

// where each team's base is, in base quadrant cells: the easiest area of the map
const SPAWN = { x: 5, y: 12, w: 3, h: 3 };
const DEFAULT_FLAGS = [[5, 12], [7, 12], [6, 14]];

const here = __dirname;
const rows = fs.readFileSync(path.join(here, "base-quadrant.txt"), "utf8")
    .split("\n").filter((l) => l.trim())
    .map((l) => [...l.matchAll(/[_a-zA-Z]/g)].map((m) => m[0]));

function fail(message) {
    console.error("gen-map: " + message);
    process.exit(1);
}

if (rows.length !== Q) fail(`base quadrant has ${rows.length} rows, expected ${Q}`);
rows.forEach((r, y) => {
    if (r.length !== Q) fail(`row ${y} has ${r.length} cells, expected ${Q}`);
    r.forEach((c, x) => {
        if (!(c in LETTERS)) fail(`unknown tile '${c}' at ${x},${y}`);
    });
});
const base = rows.map((r) => r.map((c) => LETTERS[c]));

// the frame has to be wall or the seams would not be one cell thick
for (let i = 0; i < Q; ++i) {
    if (base[i][Q - 1] !== 0) fail(`right column is not a wall at row ${i}`);
    if (base[Q - 1][i] !== 0) fail(`bottom row is not a wall at column ${i}`);
}
for (const [x, y] of DEFAULT_FLAGS)
    if (base[y][x] === 0) fail(`default flag ${x},${y} is in a wall`);
for (let y = SPAWN.y; y < SPAWN.y + SPAWN.h; ++y)
    for (let x = SPAWN.x; x < SPAWN.x + SPAWN.w; ++x)
        if (base[y][x] === 0) fail(`spawn cell ${x},${y} is a wall`);

// base quadrant coordinate -> template coordinate, for quadrant qx, qy (0 or 1)
const mapX = (x, qx) => (qx === 0 ? x : DIM - 1 - x);
const mapY = (y, qy) => (qy === 0 ? y : DIM - 1 - y);

const template = Array.from({ length: DIM }, () => new Array(DIM).fill(0));
for (let Y = 0; Y < DIM; ++Y) {
    for (let X = 0; X < DIM; ++X) {
        if (X === SEAM || Y === SEAM) continue;
        const sx = X < SEAM ? X : DIM - 1 - X;
        const sy = Y < SEAM ? Y : DIM - 1 - Y;
        template[Y][X] = base[sy][sx];
    }
}

// what a seam cell becomes when the walls drop: the harder of the open cells
// next to it, so the new lane matches the ground on both sides
const seamCells = [];
const seamValue = new Map();
const key = (x, y) => y * DIM + x;
function openNeighbour(x, y) {
    let best = 0;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
        if (nx === SEAM || ny === SEAM) continue;
        best = Math.max(best, template[ny][nx]);
    }
    return best;
}
for (let i = 0; i < DIM; ++i) {
    for (const [x, y] of [[SEAM, i], [i, SEAM]]) {
        if (seamValue.has(key(x, y))) continue;
        seamValue.set(key(x, y), openNeighbour(x, y));
        seamCells.push([x, y]);
    }
}
// a cell with no open neighbour (wall on both sides, or the crossing in the
// middle) takes the neighbouring seam cells, then a default
for (let pass = 0; pass < 4; ++pass) {
    for (const [x, y] of seamCells) {
        if (seamValue.get(key(x, y)) !== 0) continue;
        let best = 0;
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            best = Math.max(best, seamValue.get(key(x + dx, y + dy)) || 0);
        seamValue.set(key(x, y), best);
    }
}
for (const [x, y] of seamCells)
    if (seamValue.get(key(x, y)) === 0) seamValue.set(key(x, y), LETTERS.u);

function quadrantRect(qx, qy) {
    return [qx === 0 ? 0 : SEAM + 1, qy === 0 ? 0 : SEAM + 1, SEAM, SEAM];
}
function spawnRect(qx, qy) {
    const x0 = mapX(SPAWN.x, qx), x1 = mapX(SPAWN.x + SPAWN.w - 1, qx);
    const y0 = mapY(SPAWN.y, qy), y1 = mapY(SPAWN.y + SPAWN.h - 1, qy);
    return [Math.min(x0, x1), Math.min(y0, y1), SPAWN.w, SPAWN.h];
}

// quadrant order: 0 top left, 1 top right, 2 bottom left, 3 bottom right
const quadrants = [[0, 0], [1, 0], [0, 1], [1, 1]];

// ---- checks of the finished map
function check() {
    for (let Y = 0; Y < DIM; ++Y)
        for (let X = 0; X < DIM; ++X) {
            const v = template[Y][X];
            const isSeam = X === SEAM || Y === SEAM;
            if (isSeam && v !== 0) fail(`seam cell ${X},${Y} is open`);
            // four fold symmetry
            const mx = DIM - 1 - X, my = DIM - 1 - Y;
            if (template[Y][mx] !== v || template[my][X] !== v)
                fail(`map is not symmetric at ${X},${Y}`);
        }
    // seams are exactly one cell thick: the cells next to the seam line may
    // be open, but the seam line itself is one wall wide
    quadrants.forEach(([qx, qy], q) => {
        const [sx, sy, w, h] = spawnRect(qx, qy);
        for (let y = sy; y < sy + h; ++y)
            for (let x = sx; x < sx + w; ++x)
                if (template[y][x] === 0) fail(`quadrant ${q} spawn cell is a wall`);
        for (const [fx, fy] of DEFAULT_FLAGS) {
            const X = mapX(fx, qx), Y = mapY(fy, qy);
            if (template[Y][X] === 0) fail(`quadrant ${q} default flag is a wall`);
        }
    });
    // after the walls drop, every quadrant must reach every other
    const open = template.map((r) => r.slice());
    for (const [x, y] of seamCells) open[y][x] = seamValue.get(key(x, y));
    const [sx0, sy0] = spawnRect(0, 0);
    const seen = new Set([key(sx0, sy0)]);
    const stack = [[sx0, sy0]];
    while (stack.length) {
        const [x, y] = stack.pop();
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
            const nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
            if (open[ny][nx] === 0 || seen.has(key(nx, ny))) continue;
            seen.add(key(nx, ny));
            stack.push([nx, ny]);
        }
    }
    quadrants.forEach(([qx, qy], q) => {
        const [sx, sy] = spawnRect(qx, qy);
        if (!seen.has(key(sx, sy))) fail(`quadrant ${q} is not reachable after the walls drop`);
    });
    // before the walls drop, no quadrant may reach another
    const closed = template.map((r) => r.slice());
    const seen2 = new Set([key(sx0, sy0)]);
    const stack2 = [[sx0, sy0]];
    while (stack2.length) {
        const [x, y] = stack2.pop();
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
            const nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
            if (closed[ny][nx] === 0 || seen2.has(key(nx, ny))) continue;
            seen2.add(key(nx, ny));
            stack2.push([nx, ny]);
        }
    }
    quadrants.forEach(([qx, qy], q) => {
        if (q === 0) return;
        const [sx, sy] = spawnRect(qx, qy);
        if (seen2.has(key(sx, sy))) fail(`quadrant ${q} is connected to quadrant 0 while sealed`);
    });
    // team areas must be one connected region each, or players can be walled in
    const [qx0, qy0, qw, qh] = quadrantRect(0, 0);
    let openCells = 0, reached = 0;
    for (let y = qy0; y < qy0 + qh; ++y)
        for (let x = qx0; x < qx0 + qw; ++x) if (template[y][x]) ++openCells;
    for (const k of seen2) {
        const x = k % DIM, y = Math.floor(k / DIM);
        if (x < qw && y < qh) ++reached;
    }
    return { openCells, reached };
}
const stats = check();

if (process.argv.includes("--preview")) {
    for (let Y = 0; Y < DIM; ++Y)
        console.log(template[Y].map((v, X) => (X === SEAM || Y === SEAM ? "S" : v ? "." : "#")).join(""));
}
console.log(`map ${DIM}x${DIM} cells, ${seamCells.length} seam cells, ` +
            `${stats.reached}/${stats.openCells} open cells of a team area reachable from its base`);
if (process.argv.includes("--check")) process.exit(0);

// ---- Shared/CtfMap.inc
const out = [];
out.push("// generated by tools/ctf/gen-map.js from tools/ctf/base-quadrant.txt, do not edit");
out.push("// included by Shared/StaticData.c after the tile letters are defined");
out.push("");
out.push(`RR_DEFINE_MAZE(CTF, ${2 * DIM}) = {`);
for (let Y = 0; Y < DIM; ++Y)
    out.push("{" + template[Y].map((v) => NAMES[v]).join(",") + "}, // " + Y);
out.push("};");
out.push("");
out.push("// template cells, x y width height. 0 top left, 1 top right, 2 bottom left, 3 bottom right");
out.push("struct rr_ctf_rect RR_CTF_QUADRANTS[RR_CTF_QUADRANT_COUNT] = {");
for (const [qx, qy] of quadrants) out.push("    {" + quadrantRect(qx, qy).join(", ") + "},");
out.push("};");
out.push("struct rr_ctf_rect RR_CTF_SPAWNS[RR_CTF_QUADRANT_COUNT] = {");
for (const [qx, qy] of quadrants) out.push("    {" + spawnRect(qx, qy).join(", ") + "},");
out.push("};");
out.push("// where a leader's flags go if they do not place them, template cells");
out.push("uint8_t RR_CTF_DEFAULT_FLAGS[RR_CTF_QUADRANT_COUNT][RR_CTF_FLAGS_PER_TEAM][2] = {");
for (const [qx, qy] of quadrants)
    out.push("    {" + DEFAULT_FLAGS.map(([fx, fy]) => `{${mapX(fx, qx)}, ${mapY(fy, qy)}}`).join(", ") + "},");
out.push("};");
out.push("// the walls between the team areas, and what each becomes when they drop");
out.push(`uint16_t RR_CTF_SEAM_COUNT = ${seamCells.length};`);
out.push(`struct rr_ctf_seam_cell RR_CTF_SEAM_CELLS[${seamCells.length}] = {`);
for (let i = 0; i < seamCells.length; i += 6)
    out.push("    " + seamCells.slice(i, i + 6)
        .map(([x, y]) => `{${x}, ${y}, ${seamValue.get(key(x, y))}}`).join(", ") + ",");
out.push("};");
out.push("");
fs.writeFileSync(path.join(here, "..", "..", "Shared", "CtfMap.inc"), out.join("\n"));
console.log("wrote Shared/CtfMap.inc");
