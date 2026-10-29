#!/bin/bash
# Stops everything start.sh started. Safe to run when nothing is running.
# Runs on the server.

DIR="$(cd "$(dirname "$0")" && pwd)"
PORTS="${PORTS:-55554 1234 1235 1236 1237 8080}"

shopt -s nullglob
supervisors=()
children=()
for pidfile in "$DIR"/run/*.pid; do
    pid="$(cat "$pidfile")"
    if kill -0 "$pid" 2>/dev/null; then
        children+=($(pgrep -P "$pid"))
        supervisors+=("$pid")
    fi
    rm -f "$pidfile"
done

# the supervisors first, or they would restart what we stop
for pid in ${supervisors[@]+"${supervisors[@]}"}; do kill -KILL "$pid" 2>/dev/null; done
# SIGTERM lets the master server write database.json
for pid in ${children[@]+"${children[@]}"}; do kill -TERM "$pid" 2>/dev/null; done

for _ in $(seq 1 20); do
    alive=0
    for pid in ${children[@]+"${children[@]}"}; do kill -0 "$pid" 2>/dev/null && alive=1; done
    [ "$alive" = 0 ] && break
    sleep 0.5
done
for pid in ${children[@]+"${children[@]}"}; do kill -KILL "$pid" 2>/dev/null; done

# leftovers from an earlier run still holding our ports. Only processes
# running from inside this folder are touched, other services are left alone.
for port in $PORTS; do
    for pid in $(lsof -ti "tcp:$port" -sTCP:LISTEN 2>/dev/null); do
        cwd="$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')"
        case "$cwd" in
        "$DIR"/*)
            echo "killing leftover pid $pid on :$port"
            kill -TERM "$pid" 2>/dev/null
            ;;
        *) echo "port $port is used by pid $pid outside $DIR, leaving it" ;;
        esac
    done
done

echo "stopped"
