#!/bin/bash
# Runs a command forever, restarting it when it exits. Started detached by
# start.sh; its pid is what stop.sh kills first.
#
# Usage: supervise.sh <name> <dir> <command...>

name="$1"
dir="$2"
shift 2

DIR="$(cd "$(dirname "$0")" && pwd)"
log="$DIR/logs/$name.log"

cd "$dir" || exit 1
while true; do
    "$@" >> "$log" 2>&1 < /dev/null
    echo "[supervisor] $name exited with $?, restarting in 2s" >> "$log"
    sleep 2
done
