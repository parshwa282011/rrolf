#!/bin/bash
# Plays a whole capture the flag match with two bots against a real server.
#
# It builds a CTF server with -DCTF_TEST (which lets a client teleport, so it is
# never deployed) and -DPDEV like the deployed one and the bot in tools/ctf/bot.c, starts a copy of the master
# server so the real accounts database is left alone, shortens the phases, and
# runs the bot.
#
# Usage: tools/ctf/run-bot-test.sh [work dir]
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

echo "==> building in $WORK"
mkdir -p "$WORK/server"
(cd "$WORK/server" && cmake "$ROOT/Server" -DDEBUG_BUILD=0 -DCTF=1 -DCTF_TEST=1 -DPDEV=1 > cmake.log 2>&1 &&
    make -j"$(sysctl -n hw.ncpu)" > make.log 2>&1) || { echo "server build failed, see $WORK/server/make.log" >&2; exit 2; }
clang -O1 -DNDEBUG -I"$ROOT" -o "$WORK/bot" "$ROOT/tools/ctf/bot.c" \
    "$ROOT/Shared/pb.c" "$ROOT/Shared/Crypto.c" -lwebsockets 2> "$WORK/bot.log" ||
    { echo "bot build failed, see $WORK/bot.log" >&2; exit 2; }

echo "==> master server (a copy)"
mkdir -p "$WORK/master"
cp "$ROOT"/MasterServer/*.js "$ROOT"/MasterServer/package.json "$WORK/master/"
[ -f "$ROOT/MasterServer/.env" ] && cp "$ROOT/MasterServer/.env" "$WORK/master/"
[ -e "$WORK/master/node_modules" ] || ln -s "$ROOT/MasterServer/node_modules" "$WORK/master/node_modules"
# exec, so that the pid written is the process itself and not a wrapper shell
(cd "$WORK/master" && exec env PDEV=1 node main.js > "$WORK/master.log" 2>&1) &
echo $! > "$WORK/master.pid"
for _ in $(seq 1 40); do nc -z localhost 55554 2>/dev/null && break; sleep 0.25; done

echo "==> capture the flag server, phases shortened"
(cd "$WORK/server" && exec env RR_CTF_PLACEMENT_SECONDS=3 RR_CTF_FARM_SECONDS=4 RR_CTF_ENDED_SECONDS=2 \
    ./rrolf-server > "$WORK/server.log" 2>&1) &
echo $! > "$WORK/server.pid"
for _ in $(seq 1 40); do nc -z localhost 1236 2>/dev/null && break; sleep 0.25; done

echo "==> bots"
"$WORK/bot" 1236
status=$?

echo "server: $(ps -o %cpu=,rss= -p "$(cat "$WORK/server.pid")" | awk '{printf "%s%% cpu, %d MB", $1, $2 / 1024}')"
kill "$(cat "$WORK/server.pid")" "$(cat "$WORK/master.pid")" 2> /dev/null
sleep 0.5
echo "logs are in $WORK (server.log, master.log)"
exit $status
