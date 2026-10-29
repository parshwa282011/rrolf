# The Garden

The Garden is a server of its own (port 1236, build with `-DGARDEN=1`) with a
bug filled backyard map, ant holes, a snail run and the Nazar petal. This folder
has what is needed to change the map and to test the snail run.

## The map

`RR_DEFINE_MAZE(GARDEN, 80)` in `Shared/StaticData.c` is a 40x40 template, the
same format as the Hell Creek one: `_` is a wall, the letters are difficulty
(`c` easiest ... `A` hardest). One template cell is 2x2 grid cells, 2048 units.
Spawn is at the Garden Gate at the bottom, (19,36).

| zone | cells (x, y, w, h) |
|---|---|
| Garden Gate lawn | 14, 33, 12, 6 |
| Central path | 19, 10, 2, 23 |
| Pond (snail hole at 20,21) | 13, 11, 14, 10 |
| Flower beds | 2, 23, 11, 15 |
| Vegetable patch | 27, 23, 11, 14 |
| Hedge maze | 2, 2, 12, 16 |
| Compost heap | 27, 2, 11, 17 |

The zones decide what spawns where, see `garden_zone_positions` in
`Server/Simulation.c`. The spawn table is `RR_GARDEN_MOB_ID_RARITY_COEFFICIENTS`.
The checkpoints are in `RR_MAZES` just below the maze.

`gen-map.py` made the first version of the map (`python3 tools/garden/gen-map.py`
prints the block, `--preview` a picture). It was pasted into `StaticData.c` and
is edited there by hand.

`check-map.py` checks the map in `StaticData.c`: one connected region, the
checkpoints and the snail hole on open cells, and that the snail zone has cells
that can reach the hole. Run it after every change.

## The snail run

A snail is at least epic (weights 55/25/12/6/2 from epic to ancient) and starts in
the part of the map of its own rarity: epic `e`/`E`, legendary `l`/`L`, mythic
`m`/`M`, exotic `x`/`X`, ancient `a`/`A`. It sleeps there, nothing can hurt it,
and it loads nothing, until a player gets within 2500 units. Then it sets off for
the hole and from then on keeps the area around it loaded, and the mobs that
spawn around it are as rare as the snail is (`snail_difficulty` in
`struct rr_maze_grid`), so its way home stays a challenge in the easy parts as
well. If nobody comes for 8 minutes it is given up on. The numbers are `RR_SNAIL_*` in
`Shared/StaticData.h`.

`run-sim-test.sh` checks the garden's mobs on a simulation in a few seconds.

`run-bot-test.sh` plays a whole run with a bot against a real garden server: the
snail starts asleep and stays put, the bot goes to it and it wakes, it crawls to
the hole, the bot (waiting there) is rewarded and gets its mint. It needs the ports 55554 and 1236 and takes about four minutes.

`Server/Snail.c` reads a few settings from the environment, only for that
test: `RR_SNAIL_FIRST_RUN_SECONDS`, `RR_SNAIL_PERIOD_SECONDS`, `RR_SNAIL_RARITY`
(0 to 7) and `RR_SNAIL_SPEED_PERCENT`. The deployed server sets none of them. A
dev account can also start a run from the dev panel (Snail Run).

What is rewarded, and the chances, are `RR_SNAIL_*` in `Shared/StaticData.h` and
`RR_SNAIL_NAZAR_CHANCE` in `StaticData.c`.
