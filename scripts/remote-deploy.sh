#!/bin/bash
# Deploys to the server Mac and starts everything detached, so it keeps
# running after this script and ssh exit. The folder ~/rrolf-run on the server
# is deleted and recreated:
#
#   rrolf-run/master/          MasterServer (no node_modules, installed there)
#   rrolf-run/server-default/  hell creek server binary
#   rrolf-run/server-pvp/      pvp server binary
#   rrolf-run/server-garden/   garden server binary
#   rrolf-run/server-ctf/      capture the flag server binary. It is always
#                              copied, but only started with --ctf-enable
#   rrolf-run/client/          built client files and the static server
#   rrolf-run/tunnel/          cloudflared config and credentials
#   rrolf-run/logs/, run/      logs and pid files
#
# The accounts database (master/database.json) is kept: each deploy saves a
# timestamped copy in ~/rrolf-database-backup (outside rrolf-run, so the delete
# never touches it), and database.latest.json there is put back after the files
# are copied.
#
# Needs scripts/build.sh and scripts/tunnel-setup.sh to have been run here and
# scripts/remote-setup.sh against the server. Stop the stack with
# scripts/remote-stop.sh.
#
# Usage: scripts/remote-deploy.sh [-y] [--no-start] [--ctf-enable]
#   -y            do not ask before deleting the folder on the server
#   --no-start    copy the files but do not start anything
#   --ctf-enable  start the capture the flag server too (port 1237). The
#                 cloudflared tunnel runs either way

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/remote.sh"
cd "$ROOT"

assume_yes=0
start=1
ctf_enable=0
for arg in "$@"; do
    case "$arg" in
    -y) assume_yes=1 ;;
    --no-start) start=0 ;;
    --ctf-enable) ctf_enable=1 ;;
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

die() { echo "$*" >&2; exit 1; }

for f in Server/build-default/rrolf-server Server/build-pvp/rrolf-server \
         Server/build-garden/rrolf-server Server/build-ctf/rrolf-server \
         Client/public/rrolf-client.js Client/public/rrolf-client.wasm \
         Client/public/index.html Client/public/platform.js \
         Client/public/fonts/Ubuntu-Bold.ttf MasterServer/main.js; do
    [ -f "$f" ] || die "missing $f, run scripts/build.sh"
done
[ -f MasterServer/.env ] || die "missing MasterServer/.env, copy .env.example"
[ -f "$TUNNEL_CONFIG" ] || die "missing $TUNNEL_CONFIG, run scripts/tunnel-setup.sh"
check_tunnel_config || die "fix the tunnel config first"
cred="$(sed -n 's/^credentials-file: *//p' "$TUNNEL_CONFIG" | head -1)"
cred="${cred/#\~/$HOME}"
[ -f "$cred" ] || die "tunnel credentials $cred not found"

if pgrep -f "cloudflared.*$TUNNEL_NAME" > /dev/null; then
    echo "warning: this Mac is running the $TUNNEL_NAME tunnel too. With two connectors"
    echo "         Cloudflare splits traffic between them. Stop the local one."
fi

stage="$(mktemp -d)"
remote_open
trap 'rm -rf "$stage"; remote_close' EXIT

[ "$(rssh uname -m)" = "$(uname -m)" ] || die "the server's architecture differs from this Mac, the binaries will not run"
missing="$(remote_check_libs)"
[ -z "$missing" ] || die "the server is missing: $missing (run scripts/remote-setup.sh)"
rbash <<'EOF' || die "the server is missing node, cloudflared or nc (run scripts/remote-setup.sh)"
export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH
command -v node cloudflared nc > /dev/null
EOF

echo
echo "This deletes ~/$REMOTE_DIR on $REMOTE and replaces it."
if [ "$assume_yes" = 0 ]; then
    read -r -p "Continue? [y/N] " answer
    [ "$answer" = y ] || die "aborted"
fi

echo "==> staging files"
mkdir -p "$stage"/{master,server-default,server-pvp,server-garden,server-ctf,client/public/fonts,tunnel}
tar -C MasterServer --exclude=node_modules --exclude=database.json -cf - . | tar -C "$stage/master" -xf -
cp Server/build-default/rrolf-server "$stage/server-default/"
cp Server/build-pvp/rrolf-server "$stage/server-pvp/"
cp Server/build-garden/rrolf-server "$stage/server-garden/"
cp Server/build-ctf/rrolf-server "$stage/server-ctf/"
# start.sh runs the capture the flag server only when this file is there
[ "$ctf_enable" = 1 ] && touch "$stage/ctf.enabled"
cp Client/public/{index.html,platform.js,logo.png,rrolf-client.js,rrolf-client.wasm} "$stage/client/public/"
cp Client/public/fonts/* "$stage/client/public/fonts/"
cp scripts/serve-client.js "$stage/client/"
cp scripts/remote/*.sh "$stage/"
cp "$cred" "$stage/tunnel/"
remote_home="$(rssh 'echo $HOME')"
sed "s|^credentials-file:.*|credentials-file: $remote_home/$REMOTE_DIR/tunnel/$(basename "$cred")|" \
    "$TUNNEL_CONFIG" > "$stage/tunnel/tunnel.yml"

echo "==> backing up the database, then clearing ~/$REMOTE_DIR"
rbash "$REMOTE_DIR" "$REMOTE_BACKUP_DIR" <<'EOF'
set -e
dir="$HOME/$1"
bak="$HOME/$2"
[ -x "$dir/stop.sh" ] && "$dir/stop.sh"
mkdir -p "$bak"
# older deploys kept backups in ~/<dir>-backups
old="$HOME/$1-backups"
if [ -d "$old" ]; then
    for f in "$old"/*; do [ -e "$f" ] && mv "$f" "$bak/"; done
    rmdir "$old" 2> /dev/null || true
fi
if [ -f "$dir/master/database.json" ]; then
    cp "$dir/master/database.json" "$bak/database.$(date +%Y%m%d-%H%M%S).json"
    cp "$dir/master/database.json" "$bak/database.latest.json"
    echo "saved a backup in ~/$2"
else
    echo "no database to back up"
fi
rm -rf "$dir"
mkdir -p "$dir"
EOF

echo "==> copying"
rscp -r "$stage"/* "$REMOTE:$REMOTE_DIR/"

echo "==> restoring the database"
rbash "$REMOTE_DIR" "$REMOTE_BACKUP_DIR" <<'EOF'
set -e
if [ -f "$HOME/$2/database.latest.json" ]; then
    cp "$HOME/$2/database.latest.json" "$HOME/$1/master/database.json"
    echo "restored the accounts database"
else
    echo "no backup to restore, the master server starts empty"
fi
EOF

echo "==> installing and starting"
rbash "$REMOTE_DIR" "$start" <<'EOF'
set -e
export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH
dir="$HOME/$1"
chmod +x "$dir"/*.sh "$dir"/server-*/rrolf-server
chmod 600 "$dir"/master/.env "$dir"/tunnel/*.json
(cd "$dir/master" && npm install --omit=dev --no-audit --no-fund)
if [ "$2" = 1 ]; then "$dir/start.sh"; fi
EOF

echo
if [ "$start" = 1 ]; then
    echo "running on $REMOTE, detached. Public: https://$HOST_WEB"
    [ "$ctf_enable" = 1 ] && echo "capture the flag is on (:1237)" || echo "capture the flag is shipped but off, --ctf-enable starts it"
    echo "logs:  ssh $REMOTE tail -f $REMOTE_DIR/logs/'*.log'"
    echo "stop:  scripts/remote-stop.sh"
else
    echo "copied to $REMOTE:~/$REMOTE_DIR, not started"
fi
