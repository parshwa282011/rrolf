#!/bin/bash
# Run script for rrolf (macOS)
#
# By default:
#   1. Builds everything via ./build.sh
#   2. Starts the MasterServer (accounts/API, port 55554)
#   3. Starts the game servers (rrolf-server): hell_creek_easy on port 6767,
#      hell_creek_med on port 6768
#   4. Hosts the client on http://localhost:8080
#
# Options:
#   -s, --server-only       Only run the MasterServer + game server(s) - skip
#                           building and hosting the web client.
#   -b, --biome <biome>     Which biome(s) to host: easy, med, or both
#                           (default: both).
#   -h, --help              Show this help.
#
# Examples:
#   ./run.sh                       # everything, both biomes
#   ./run.sh --server-only         # servers only, both biomes
#   ./run.sh -s -b easy            # server only, just hell_creek_easy
#   ./run.sh --biome med           # everything, but only hell_creek_med

set -euo pipefail

server_only=0
biome="both"

usage() {
    sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
    SKIP_CLIENT=1 "$ROOT/build.sh"
else
    "$ROOT/build.sh"
fi

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
    echo "rrolf server(s) running (MasterServer on 55554)."
else
    echo "==> Hosting client on http://localhost:8080"
    (cd "$ROOT/Client/build" && exec python3 -m http.server 8080) &
    pids+=($!)

    echo
    echo "rrolf is running: open http://localhost:8080 in your browser."
fi

echo "Press Ctrl+C to stop all servers."

wait
