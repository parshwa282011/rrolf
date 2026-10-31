# Capture the flag

A minigame on its own server (`-DCTF`, port 1237, `ctf-game.parshwa.blog`). Nothing is saved: everyone
starts at level 30 (6 slots) with 5 Epic Basic and 1 Epic Azalea and the account is untouched. The
Give Petals button (PIN) works in a match, the server is built with `-DPDEV`.

## How a match goes

1. Everyone picks **CTF** on the menu. Each team makes a **private squad** (Create Squad).
2. The host (leader of one of the squads) opens the **flag button** (top left), types the other squads' codes
   (up to 3) and presses **Link squads**, then **Start** (click twice).
3. **Placement (60 s).** Everyone is in their team's area. Each squad leader presses **F** where they stand
   to place a flag point, three of them (F again moves them in turn). Unplaced points use defaults.
4. **Sealed (10 min).** The walls between the areas are up. Farm mobs, craft (**C**), change the loadout
   (open the inventory with **Z**, then drag a petal onto a slot, or click it to fill the first empty one).
   A loadout change is on your flower at once, there is no respawn button. Dying respawns you at your
   base after a second.
5. **War.** The walls drop. Take another team's flag by touching it and carry it to any of **your** flag
   points to bank it. A team stays in the game while it holds at least one flag and is out at once when it
   holds none. A team that lost a flag can stand on its empty flag point for 3 s to set a banked flag up
   there. The first team to hold every other team's flags wins, then the server goes back to the lobby.

A flag falls where its carrier died (or left), at the exact spot.

"Holding" means the flags standing on your points plus the ones you banked. A carried or dropped flag
still counts for the team it was taken from. A dropped flag goes home after 15 s, or at once when its own
team touches it. A team that is out hands what it banked to the team that finished it.

The minimap shows the 40x40 cells around you. Hover over it, or press **N** (again to turn it off), to
see the whole map.

## Losing your connection

A player who drops keeps their place in the squad and the match, and everything they farmed, for
10 minutes (`RR_CTF_RECONNECT_SECONDS` in `Shared/Ctf.h`). The page reconnects by itself; the server finds the
same account, hands the old flower and inventory to the new connection and puts the player back in the
game (a flag they carried dropped where they were). After 10 minutes the place is freed.

## Nothing is kept

The second a match ends for any reason (a team wins, the host cancels, everybody leaves) every player is
set back to level 30 with 5 Epic Basic and 1 Epic Azalea: connected or not, crafted petals, farmed petals,
loadout and all (`reset_players` in `Server/Ctf.c`). Someone who comes back after the end gets the starting
petals when they connect.

## Chat and squads across servers

Every game server sends its chat and the list of its squads to the master server, which hands them to the
others. A chat line from another server shows as `[hc] Name: text`. The squad panel has an "Other servers" list
with `[server] CODE leader members`, for squads whose code is shown (a squad that hides its code stays
"(private)").

## Clean worlds

Every player's server record tracks which entities the *client* has, so it can tell the client to delete
them later. A respawn or a reconnect makes a new record that thinks the client has nothing, while the
client still shows its old world, and mobs and drops that were gone from the server stayed on screen. So the
update that puts a player (back) into the game says `in game = 2`, and the client throws its whole world
away before reading it (`Server/Server.c`, `Client/Game.c`). Between matches the arena's mobs and drops are
deleted, and during one a drop nobody connected can pick up is deleted each second.

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

`bot.c` also runs against the C master server (which wants real accounts): sign two accounts up and give
the bot `BOT_UUID_A`, `BOT_TOKEN_A` (the PL-P), `BOT_UUID_B`, `BOT_TOKEN_B` in the environment. With
`BOT_EXPECT_OTHER_SERVER=1` it also checks the squad list and chat of a second game server (named `rrolf-hc`
with a squad `HC-zzzzzz` led by `Zed`, saying "hi from hc").

The bot test builds a `-DCTF_TEST` server (it lets a client teleport, never deploy it) and runs a copy of
the master server, so the real accounts database is not touched. Ports 55554 and 1237 have to be free.

## Trying it by hand

Start a server with short phases, for example
`RR_CTF_PLACEMENT_SECONDS=15 RR_CTF_FARM_SECONDS=60 ./rrolf-server`. The map is about 80,000 units across and
the walls only drop at the end of the sealed phase, so use the test build and its teleport to cross it.

## Changing the rules

The numbers (radii, timers, minimum flags) are the `RR_CTF_*` defines in `Shared/Ctf.h`. The start level
and petals are `RR_CTF_START_*` in `Shared/StaticData.h`. To change what the
walls do, edit `rr_ctf_drop_walls` in `Shared/Ctf.c`. To change the map, edit `base-quadrant.txt` and run
the generator. The server and the client both compile the map in, so rebuild both.
