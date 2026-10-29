#!/bin/bash
# Starts the whole stack detached, so it keeps running after ssh closes.
# Runs on the server. Logs go to logs/<name>.log, pids to run/<name>.pid.

# a non-interactive ssh shell does not have homebrew on its PATH
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || exit 1

"$DIR/stop.sh" > /dev/null
mkdir -p logs run
: > logs/master.log
for name in master default pvp garden ctf web tunnel awake; do : > "logs/$name.log"; done

# spawn <name> <dir> <command...>
spawn() {
    local name="$1"
    nohup "$DIR/supervise.sh" "$@" > /dev/null 2>&1 < /dev/null &
    echo $! > "run/$name.pid"
}

# keep the Mac awake while the stack is up
spawn awake . caffeinate -i -s

spawn master master env PDEV=1 node main.js
# the game servers abort if the master server is not listening yet
for _ in $(seq 1 60); do
    nc -z localhost 55554 2>/dev/null && break
    sleep 0.5
done
nc -z localhost 55554 2>/dev/null || { echo "master server did not start, see logs/master.log" >&2; exit 1; }

spawn default server-default ./rrolf-server
spawn pvp server-pvp ./rrolf-server
spawn garden server-garden ./rrolf-server
# capture the flag is shipped with every deploy but runs only when the deploy
# was made with --ctf-enable, which leaves this file
if [ -f ctf.enabled ]; then
    spawn ctf server-ctf ./rrolf-server
    ports="1237"
else
    echo "capture the flag is off (deploy with --ctf-enable to run it)"
fi
spawn web client env CLIENT_DIR="$DIR/client/public" PORT=8080 node serve-client.js
spawn tunnel tunnel cloudflared tunnel --config "$DIR/tunnel/tunnel.yml" run

sleep 3
for port in 55554 1234 1235 1236 ${ports:-} 8080; do
    nc -z localhost "$port" 2>/dev/null && echo "up   :$port" || echo "DOWN :$port (see logs/)"
done
