#!/usr/bin/env python3
"""Lays out the Garden map and prints it as the RR_DEFINE_MAZE(GARDEN, 80) block
that lives in Shared/StaticData.c.

    python3 tools/garden/gen-map.py            print the block
    python3 tools/garden/gen-map.py --preview  print a plain text picture instead

The block is a one-off starting point: after it is pasted into StaticData.c the
map is edited there by hand, and tools/garden/check-map.py checks the result.
Open cells are 1 while carving, then each one gets a difficulty letter from its
walking distance to the spawn, so difficulty rises away from the Garden Gate.
"""
import collections
import random
import sys

N = 40
LETTERS = "cCuUrReElLmMxXaA"
SPAWN = (19, 36)

random.seed(7)
g = [[0] * N for _ in range(N)]


def rect(x0, y0, x1, y1, v=1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            g[y][x] = v


# gate lawn, with four pillars
rect(14, 33, 25, 38)
for x, y in [(17, 35), (22, 35), (17, 37), (22, 37)]:
    g[y][x] = 0
# central path up to the pond
rect(19, 10, 20, 32)
# flower beds (west): 3x3 beds on a 4 cell pitch, one cell hedge gaps
for by in range(23, 36, 4):
    for bx in range(2, 12, 4):
        rect(bx, by, bx + 2, by + 2)
        if bx + 4 <= 10:
            g[by + 1][bx + 3] = 1
        if by + 4 <= 35:
            g[by + 3][bx + 1] = 1
g[34][12] = g[34][13] = 1  # beds to the gate lawn
g[22][3] = g[21][3] = g[20][3] = 1  # beds north exit
# vegetable patch (east): seven serpentine rows
rows = list(range(23, 36, 2))
for i, y in enumerate(rows):
    rect(27, y, 37, y)
    if i < len(rows) - 1:
        g[y + 1][37 if i % 2 == 0 else 27] = 1
g[35][26] = g[35][25] = 1  # first row to the gate lawn
rect(27, 34, 28, 35)
g[22][33] = g[21][33] = g[20][33] = 1  # veg patch north exit
# pond (centre north) with an island and stepping stones
rect(13, 11, 26, 20)
rect(17, 14, 22, 17, 0)
rect(19, 14, 20, 17)
rect(19, 21, 20, 21)
# hedge maze (north west): recursive backtracker, 6x8 cells at pitch 2
MX, MY = 6, 8
seen = set()


def carve(cx, cy):
    seen.add((cx, cy))
    x, y = 2 + cx * 2, 2 + cy * 2
    g[y][x] = 1
    d = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    random.shuffle(d)
    for dx, dy in d:
        nx, ny = cx + dx, cy + dy
        if 0 <= nx < MX and 0 <= ny < MY and (nx, ny) not in seen:
            g[y + dy][x + dx] = 1
            carve(nx, ny)


carve(0, 0)
g[14][12] = g[14][13] = 1  # maze east side to the pond
g[17][13] = 0              # keep the pond's west wall solid here
for y in range(17, 23):    # maze south side to the flower beds
    g[y][2] = 1
g[20][3] = g[22][3] = 1
# compost heap (north east): big arena with pillars
rect(27, 2, 37, 18)
for y in range(4, 17, 3):
    for x in range(29, 37, 3):
        g[y][x] = 0
g[15][26] = 1                 # pond east to the compost heap
rect(33, 19, 33, 22)          # veg patch north to the compost heap
rect(19, 10, 20, 10)


def bfs(start):
    dist = {start: 0}
    q = collections.deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if 0 <= n[0] < N and 0 <= n[1] < N and g[n[1]][n[0]] and n not in dist:
                dist[n] = dist[(x, y)] + 1
                q.append(n)
    return dist


dist = bfs(SPAWN)
far = max(dist.values())
assert len(dist) == sum(map(sum, g)), "unreachable cells"


def letter(x, y):
    return "_" if not g[y][x] else LETTERS[min(15, dist[(x, y)] * 16 // (far + 1))]


if "--preview" in sys.argv:
    for y in range(N):
        print("".join("#" if not g[y][x] else letter(x, y) for x in range(N)))
    sys.exit()

print("RR_DEFINE_MAZE(GARDEN, 80) = {")
print("//                     11  13  15  17  19  21  23  25  27  29  31  33  35  37  39")
print("// 1 2 3 4 5 6 7 8 9 10  12  14  16  18  20  22  24  26  28  30  32  34  36  38")
for y in range(N):
    print("{%s}, // %d" % (",".join(letter(x, y) for x in range(N)), y))
print("};")
