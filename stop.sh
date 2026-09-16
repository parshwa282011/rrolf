#!/bin/bash
# Stops any locally running rrolf processes started by run.sh:
#   - MasterServer (node main.js, port 55554)
#   - game servers (rrolf-server, ports 6767 and 6768)
#   - client static server (port 8080)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
stopped_any=0

stop_by_port() {
    local port="$1"
    local pids
    pids="$(lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null || true)"
    if [ -n "$pids" ]; then
        echo "==> Stopping process(es) on port $port: $pids"
        kill $pids 2>/dev/null
        stopped_any=1
    fi
}

stop_by_pattern() {
    local pattern="$1"
    local pids
    pids="$(pgrep -f "$pattern" 2>/dev/null || true)"
    if [ -n "$pids" ]; then
        echo "==> Stopping process(es) matching '$pattern': $pids"
        kill $pids 2>/dev/null
        stopped_any=1
    fi
}

stop_by_port 55554
stop_by_port 6767
stop_by_port 6768
stop_by_port 8080

# Fallback in case something didn't bind the expected port yet.
stop_by_pattern "MasterServer/main.js"
stop_by_pattern "$ROOT/Server/build/rrolf-server"
stop_by_pattern "http.server 8080"

if [ "$stopped_any" -eq 0 ]; then
    echo "No running rrolf servers found."
else
    echo "==> Done."
fi
