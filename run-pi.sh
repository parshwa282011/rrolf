#!/bin/bash
# Run script for rrolf (Raspberry Pi / Debian-based arm/arm64)
#
# This is the Raspberry Pi counterpart to run.sh (which is macOS-only).
# By default:
#   1. Builds everything via ./build-pi.sh (server always; client only if
#      emscripten is installed - see setup-pi.sh --with-client)
#   2. Starts the MasterServer (accounts/API, port 55554)
#   3. Starts the game servers (rrolf-server): hell_creek_easy on port 6767,
#      hell_creek_med on port 6768
#   4. Hosts the client on port 8080, if it was built
#
# First-time setup: ./setup-pi.sh (installs cmake, make, clang, Node.js,
# libwebsockets, etc.)
#
# Options:
#   -s, --server-only       Only run the MasterServer + game server(s) - skip
#                           building/hosting the web client.
#   -b, --biome <biome>     Which biome(s) to host: easy, med, or both
#                           (default: both).
#   -h, --help              Show this help.
#
# Examples:
#   ./run-pi.sh                    # everything this Pi is able to build, both biomes
#   ./run-pi.sh --server-only      # servers only, both biomes
#   ./run-pi.sh -s -b easy         # server only, just hell_creek_easy
#   ./run-pi.sh --biome med        # everything, but only hell_creek_med

set -euo pipefail

server_only=0
biome="both"

usage() {
    sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
    -s | --server-only)
        server_only=1
        shift
        ;;
    -b | --biome)
        biome="${2:-}"
        shift 2
        ;;
    --biome=*)
        biome="${1#--biome=}"
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
done

case "$biome" in
easy | med | both) ;;
*)
    echo "Invalid --biome '$biome' (expected easy, med, or both)" >&2
    exit 1
    ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [ "$server_only" = "1" ]; then
    SKIP_CLIENT=1 "$ROOT/build-pi.sh"
else
    "$ROOT/build-pi.sh"
fi

lan_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
lan_ip="${lan_ip:-<this-Pi-IP>}"

pids=()
cleanup() {
    echo
    echo "==> Shutting down..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT INT TERM

echo "==> Starting MasterServer on port 55554"
(cd "$ROOT/MasterServer" && exec node main.js) &
pids+=($!)

if [ "$biome" = "easy" ] || [ "$biome" = "both" ]; then
    echo "==> Starting game server (hell_creek_easy) on port 6767"
    (cd "$ROOT/Server/build" && exec env RR_PORT=6767 RR_BIOME=hell_creek_easy ./rrolf-server) &
    pids+=($!)
fi

if [ "$biome" = "med" ] || [ "$biome" = "both" ]; then
    echo "==> Starting game server (hell_creek_med) on port 6768"
    (cd "$ROOT/Server/build" && exec env RR_PORT=6768 RR_BIOME=hell_creek_med ./rrolf-server) &
    pids+=($!)
fi

sleep 1

if [ "$server_only" = "1" ]; then
    echo
    echo "rrolf server(s) running (MasterServer on $lan_ip:55554)."
elif [ -d "$ROOT/Client/build" ]; then
    echo "==> Hosting client on port 8080"
    (cd "$ROOT/Client/build" && exec python3 -m http.server 8080) &
    pids+=($!)

    echo
    echo "rrolf is running: open http://$lan_ip:8080 from any device on your network."
else
    echo
    echo "rrolf server(s) running (MasterServer on $lan_ip:55554), but Client/build"
    echo "doesn't exist so there's no client to host. Either build it elsewhere and"
    echo "copy Client/build here, or run ./setup-pi.sh --with-client && ./build-pi.sh."
fi

echo "Press Ctrl+C to stop all servers."

wait
