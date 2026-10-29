#!/bin/bash
# Runs the whole stack locally: MasterServer, the game servers and the static
# client host. The capture the flag server only runs with --ctf-enable. With --tunnel it also starts cloudflared so the game is
# reachable at https://$HOST_WEB.
#
# Usage: scripts/dev-up.sh [--tunnel] [--build] [--ctf-enable]
#   --tunnel      also run the cloudflared tunnel (see scripts/tunnel-setup.sh)
#   --build       run scripts/build.sh first
#   --ctf-enable  also run the capture the flag server (port $PORT_CTF)

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"
cd "$ROOT"

tunnel=0
build=0
ctf=0
for arg in "$@"; do
    case "$arg" in
    --tunnel) tunnel=1 ;;
    --build) build=1 ;;
    --ctf-enable) ctf=1 ;;
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

[ "$build" = 1 ] && scripts/build.sh

for f in Server/build-default/rrolf-server Server/build-pvp/rrolf-server \
         Server/build-garden/rrolf-server Server/build-ctf/rrolf-server \
         Client/public/rrolf-client.js Client/public/rrolf-client.wasm \
         MasterServer/node_modules; do
    [ -e "$f" ] || { echo "missing $f, run scripts/build.sh" >&2; exit 1; }
done
[ -f MasterServer/.env ] || echo "warning: MasterServer/.env missing, copy .env.example" >&2

ports="$PORT_MASTER $PORT_DEFAULT $PORT_PVP $PORT_GARDEN $PORT_WEB"
[ "$ctf" = 1 ] && ports="$ports $PORT_CTF"
for port in $ports; do
    if nc -z localhost "$port" 2>/dev/null; then
        echo "port $port is already in use" >&2
        exit 1
    fi
done

# kill every process this script started, including the piped log prefixers
trap 'trap - EXIT INT TERM; echo; echo "==> shutting down"; kill 0' EXIT INT TERM

# run <label> <dir> <command...>: run in the background with prefixed output
run() {
    local label="$1" dir="$2"
    shift 2
    (cd "$dir" && exec "$@") 2>&1 | sed -l "s/^/[$label] /" &
}

echo "==> MasterServer on :$PORT_MASTER"
run master MasterServer env PDEV=1 node main.js
# the game servers abort if the master server is not up yet
until nc -z localhost "$PORT_MASTER" 2>/dev/null; do sleep 0.3; done

echo "==> hell creek server on :$PORT_DEFAULT"
run default Server/build-default ./rrolf-server
echo "==> pvp server on :$PORT_PVP"
run pvp Server/build-pvp ./rrolf-server
echo "==> garden server on :$PORT_GARDEN"
run garden Server/build-garden ./rrolf-server
if [ "$ctf" = 1 ]; then
    echo "==> capture the flag server on :$PORT_CTF"
    run ctf Server/build-ctf ./rrolf-server
else
    echo "==> capture the flag server is off (--ctf-enable runs it)"
fi
echo "==> client on :$PORT_WEB"
PORT="$PORT_WEB" run web . node scripts/serve-client.js

if [ "$tunnel" = 1 ]; then
    echo "==> tunnel"
    run tunnel . scripts/tunnel.sh
fi

echo
echo "local:  http://localhost:$PORT_WEB"
echo "local with the CrazyGames test SDK: http://localhost:$PORT_WEB/?useLocalSdk=true"
[ "$tunnel" = 1 ] && echo "public: https://$HOST_WEB"
echo "Ctrl+C stops everything."
wait
