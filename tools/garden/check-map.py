#!/usr/bin/env python3
"""Checks the Garden map that lives in Shared/StaticData.c.

    python3 tools/garden/check-map.py

It parses RR_DEFINE_MAZE(GARDEN, 80) and the Garden checkpoints out of the
source and fails if: the map is not one connected region, a checkpoint or its
respawn point is not on open cells, the snail hole or a snail start is on a
wall, or there is no cell a snail of some rarity can start in. A snail starts in
a random open cell of its own rarity's letters of the map (RR_SNAIL_ZONE_* in
Shared/StaticData.h: epic e/E ... ancient a/A) that can reach the hole.
"""
import collections
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SRC = open(os.path.join(ROOT, "Shared", "StaticData.c")).read()
N = 40

HOLE = (20, 21)
SPAWN = (19, 36)
HEADER = open(os.path.join(ROOT, "Shared", "StaticData.h")).read()
LETTERS = {"_": 0, "c": 1, "C": 4, "u": 8, "U": 12, "r": 16, "R": 20, "e": 24,
           "E": 28, "l": 32, "L": 36, "m": 40, "M": 44, "x": 48, "X": 52,
           "a": 56, "A": 60}

# rarity -> template difficulty range a snail of it starts in (epic is 3): its
# own letters, 8 * rarity to 8 * rarity + 4. Same as RR_SNAIL_ZONE_MIN/MAX in
# Shared/StaticData.h, which the script checks below
ZONES = {name: (8 * r, 8 * r + 4) for r, name in
         enumerate(["common", "unusual", "rare", "epic", "legendary", "mythic", "exotic", "ancient"]) if r >= 3}
assert "RR_SNAIL_ZONE_MIN(rarity) (8 * (rarity))" in HEADER and "RR_SNAIL_ZONE_MAX(rarity) (8 * (rarity) + 4)" in HEADER

block = re.search(r"RR_DEFINE_MAZE\(GARDEN, 80\) = \{(.*?)\n\};", SRC, re.S).group(1)
rows = [r.group(1).split(",") for r in re.finditer(r"^\{([^}]*)\},", block, re.M)]
assert len(rows) == N and all(len(r) == N for r in rows), "map is not 40x40"
open_ = [[c != "_" for c in r] for r in rows]

cps = re.search(r"MAZE_ENTRY\(GARDEN, 1024\), (\d+), \{(.*?)\n    \}\}", SRC, re.S)
count = int(cps.group(1))
checkpoints = [tuple(int(v) for v in m.groups())
               for m in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}", cps.group(2))]
errors = []
if len(checkpoints) != count:
    errors.append("checkpoint_count %d but %d listed" % (count, len(checkpoints)))


def walk(start):
    dist = {start: 0}
    q = collections.deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < N and 0 <= ny < N and open_[ny][nx] and (nx, ny) not in dist:
                dist[(nx, ny)] = dist[(x, y)] + 1
                q.append((nx, ny))
    return dist


from_spawn = walk(SPAWN)
total = sum(map(sum, open_))
if len(from_spawn) != total:
    errors.append("%d of %d open cells are not reachable from the spawn" % (total - len(from_spawn), total))

for i, (x, y, w, h, sx, sy, level) in enumerate(checkpoints):
    if not all(open_[yy][xx] for yy in range(y, y + h) for xx in range(x, x + w)):
        errors.append("checkpoint %d zone is not all open cells" % i)
    if not (x <= sx < x + w and y <= sy < y + h and open_[sy][sx]):
        errors.append("checkpoint %d respawn point is not an open cell inside its zone" % i)
if checkpoints and checkpoints[0][:2] != (18, 34):
    errors.append("checkpoint 0 should be the gate lawn")

if not open_[HOLE[1]][HOLE[0]]:
    errors.append("snail hole %s is a wall" % (HOLE,))
from_hole = walk(HOLE)
for name, (zmin, zmax) in ZONES.items():
    zone = [(x, y) for y in range(N) for x in range(N)
            if zmin <= LETTERS[rows[y][x]] <= zmax]
    routes = [from_hole[c] for c in zone if c in from_hole]
    if not routes:
        errors.append("no open cell a %s snail can start in (difficulty %d to %d)" % (name, zmin, zmax))
    elif len(routes) != len(zone):
        errors.append("%d cells of the %s snail zone can't reach the hole" % (len(zone) - len(routes), name))
    else:
        print("%s snail: %d cells to start in, %d to %d cells from the hole" % (name, len(zone), min(routes), max(routes)))
        if max(routes) > 200:
            errors.append("a %s snail would have a route of %d cells, the search keeps 255 at most" % (name, max(routes)))

print("%d open cells, %d checkpoints, hole %s" % (total, len(checkpoints), HOLE))
for e in errors:
    print("FAIL:", e)
sys.exit(1 if errors else 0)
