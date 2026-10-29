#!/bin/bash
# Plays a whole snail run with a bot against a real garden server.
#
# It builds a garden server with -DGARDEN and -DPDEV like the deployed one plus
# -DCTF_TEST, which lets a client teleport so the bot can wait at the hole (that
# build is never deployed), and the bot in tools/garden/bot.c, starts a copy of the master server so the real
# accounts database is left alone, makes the snail run start at once, fast and
# of a fixed rarity, and runs the bot.
#
# Usage: tools/garden/run-bot-test.sh [work dir]
#   needs the ports 55554 and 1236 to be free

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${1:-$(mktemp -d)}"
mkdir -p "$WORK"

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
export CPATH="$BREW_PREFIX/opt/curl/include:$BREW_PREFIX/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$BREW_PREFIX/opt/curl/lib:$BREW_PREFIX/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"

for port in 55554 1236; do
    if nc -z localhost "$port" 2>/dev/null; then
        echo "port $port is in use, stop whatever runs there first" >&2
        exit 2
    fi
done

echo "==> the map"
python3 -I "$ROOT/tools/garden/check-map.py" || exit 2

echo "==> building in $WORK"
mkdir -p "$WORK/server"
(cd "$WORK/server" && cmake "$ROOT/Server" -DDEBUG_BUILD=0 -DGARDEN=1 -DCTF_TEST=1 -DPDEV=1 > cmake.log 2>&1 &&
    make -j"$(sysctl -n hw.ncpu)" > make.log 2>&1) || { echo "server build failed, see $WORK/server/make.log" >&2; exit 2; }
clang -O1 -DNDEBUG -I"$ROOT" -o "$WORK/bot" "$ROOT/tools/garden/bot.c" \
    "$ROOT/Shared/pb.c" "$ROOT/Shared/Crypto.c" -lwebsockets 2> "$WORK/bot.log" ||
    { echo "bot build failed, see $WORK/bot.log" >&2; exit 2; }

echo "==> master server (a copy)"
mkdir -p "$WORK/master"
cp "$ROOT"/MasterServer/*.js "$ROOT"/MasterServer/package.json "$WORK/master/"
[ -f "$ROOT/MasterServer/.env" ] && cp "$ROOT/MasterServer/.env" "$WORK/master/"
[ -e "$WORK/master/node_modules" ] || ln -s "$ROOT/MasterServer/node_modules" "$WORK/master/node_modules"
(cd "$WORK/master" && exec env PDEV=1 node main.js > "$WORK/master.log" 2>&1) &
echo $! > "$WORK/master.pid"
for _ in $(seq 1 40); do nc -z localhost 55554 2>/dev/null && break; sleep 0.25; done

# an epic snail: a mint is always part of the reward. Six times as fast as in
# the game so that the run is over in about a minute
echo "==> garden server, the snail run starts at once"
(cd "$WORK/server" && exec env RR_TEST_NO_SPAWNS=1 RR_SNAIL_FIRST_RUN_SECONDS=8 RR_SNAIL_RARITY=3 RR_SNAIL_SPEED_PERCENT=600 \
    RR_SNAIL_PERIOD_SECONDS=600 ./rrolf-server > "$WORK/server.log" 2>&1) &
echo $! > "$WORK/server.pid"
for _ in $(seq 1 40); do nc -z localhost 1236 2>/dev/null && break; sleep 0.25; done

echo "==> bot"
"$WORK/bot" 1236
status=$?

echo "server: $(ps -o %cpu=,rss= -p "$(cat "$WORK/server.pid")" | awk '{printf "%s%% cpu, %d MB", $1, $2 / 1024}')"
kill "$(cat "$WORK/server.pid")" "$(cat "$WORK/master.pid")" 2> /dev/null
sleep 0.5
echo "logs are in $WORK (server.log, master.log)"
exit $status
