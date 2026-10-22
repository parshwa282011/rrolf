# Capture the flag

A minigame on its own server (`-DCTF`, port 1236, `ctf-game.parshwa.blog`). Nothing is saved: everyone
starts at level 10 with 5 Epic Basic petals and the account is untouched.

## How a match goes

1. Everyone picks **CTF** on the menu. Each team makes a **private squad** (Create Squad).
2. The host (leader of one of the squads) opens the **flag button** (top left), types the other squads' codes
   (up to 3) and presses **Link squads**, then **Start** (click twice).
3. **Placement (60 s).** Everyone is in their team's area. Each squad leader presses **F** where they stand
   to place a flag point, three of them (F again moves them in turn). Unplaced points use defaults.
4. **Sealed (10 min).** The walls between the areas are up. Farm mobs, craft (**C**), change the loadout
   (**Z**, then *Respawn with this loadout*). Dying respawns you at your base after a second.
5. **War.** The walls drop. Take another team's flag by touching it and carry it to any of **your** flag
   points to bank it. A team holding fewer than 3 flags is out at once. A team that lost a flag can stand on
   its empty flag point for 3 s to set a banked flag up there. The first team to hold every other team's
   flags wins, then the server goes back to the lobby.

"Holding" means the flags standing on your points plus the ones you banked. A carried or dropped flag
still counts for the team it was taken from. A dropped flag goes home after 15 s, or at once when its own
team touches it. A team that is out hands what it banked to the team that finished it.

## Files

| | |
|---|---|
| `Server/CtfRules.c` | the rules, no server in it. Tested by `test-rules.c` |
| `Server/Ctf.c` | lobby, phases, spawning, respawn, the state packet |
| `Shared/Ctf.c`, `Shared/Ctf.h` | team areas, colours, dropping the seam walls, tuning numbers |
| `Shared/CtfMap.inc` | the map, **generated** by `gen-map.js` from `base-quadrant.txt` |
| `Client/Ctf.c` | state, flags in the world, minimap, the F key |
| `Client/Ui/InGame/CtfHud.c`, `Client/Ui/TitleScreen/CtfHost.c` | HUD, loadout editor, host panel |

## Tools

```
node tools/ctf/gen-map.js --check      check the map (symmetry, 1 cell seams, connectivity)
node tools/ctf/gen-map.js --preview    print it
node tools/ctf/gen-map.js              rewrite Shared/CtfMap.inc
cc -I. -o /tmp/ctf-test tools/ctf/test-rules.c Server/CtfRules.c && /tmp/ctf-test
tools/ctf/run-bot-test.sh              two bots play a whole match against a real server
```

The bot test builds a `-DCTF_TEST` server (it lets a client teleport, never deploy it) and runs a copy of
the master server, so the real accounts database is not touched. Ports 55554 and 1236 have to be free.

## Trying it by hand

Start a server with short phases, for example
`RR_CTF_PLACEMENT_SECONDS=15 RR_CTF_FARM_SECONDS=60 ./rrolf-server`. The map is about 80,000 units across and
the walls only drop at the end of the sealed phase, so use the test build and its teleport to cross it.

## Changing the rules

The numbers (radii, timers, minimum flags) are the `RR_CTF_*` defines in `Shared/Ctf.h`. To change what the
walls do, edit `rr_ctf_drop_walls` in `Shared/Ctf.c`. To change the map, edit `base-quadrant.txt` and run
the generator. The server and the client both compile the map in, so rebuild both.
