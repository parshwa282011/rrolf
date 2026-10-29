# Helpers for the remote-*.sh scripts. Sourced, not executed.
#
# The server login is asked once per script run: the first ssh opens a shared
# connection that every later ssh/scp reuses, so the password is typed once.
# The password itself is never stored. The host and user are saved in
# scripts/remote.env (gitignored) after the first prompt.

source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

REMOTE_ENV="$ROOT/scripts/remote.env"
# shellcheck disable=SC1090
[ -f "$REMOTE_ENV" ] && source "$REMOTE_ENV"

if [ -z "${REMOTE_HOST:-}" ] || [ -z "${REMOTE_USER:-}" ]; then
    read -r -p "Server host or IP: " REMOTE_HOST
    read -r -p "Server user: " REMOTE_USER
    printf 'REMOTE_HOST=%s\nREMOTE_USER=%s\n' "$REMOTE_HOST" "$REMOTE_USER" > "$REMOTE_ENV"
    echo "saved to $REMOTE_ENV"
fi

# folder on the server, relative to its home. The deploy deletes it, so keep
# the name boring.
REMOTE_DIR="${REMOTE_DIR:-rrolf-run}"
[[ "$REMOTE_DIR" =~ ^[A-Za-z0-9._-]+$ && "$REMOTE_DIR" != . && "$REMOTE_DIR" != .. ]] ||
    { echo "bad REMOTE_DIR '$REMOTE_DIR'" >&2; exit 1; }

# where database backups live on the server, also relative to its home and
# kept outside REMOTE_DIR so the deploy's delete never touches it
REMOTE_BACKUP_DIR="${REMOTE_BACKUP_DIR:-rrolf-database-backup}"
[[ "$REMOTE_BACKUP_DIR" =~ ^[A-Za-z0-9._-]+$ && "$REMOTE_BACKUP_DIR" != . && "$REMOTE_BACKUP_DIR" != .. &&
   "$REMOTE_BACKUP_DIR" != "$REMOTE_DIR" ]] ||
    { echo "bad REMOTE_BACKUP_DIR '$REMOTE_BACKUP_DIR'" >&2; exit 1; }

mkdir -p "$HOME/.ssh"
SSH_OPTS=(
    -o ControlMaster=auto
    -o "ControlPath=$HOME/.ssh/rrolf-cm-%C"
    -o ControlPersist=10m
    -o ServerAliveInterval=30
)
# Use the dedicated deploy key when it exists, so no password is asked. Create
# it with:
#   ssh-keygen -t ed25519 -f ~/.ssh/id_c1 -N "" -C rrolf-deploy
#   ssh-copy-id -i ~/.ssh/id_c1.pub user@host
# Without the file this falls back to the usual ssh login (password prompt).
REMOTE_KEY="${REMOTE_KEY:-$HOME/.ssh/id_c1}"
if [ -f "$REMOTE_KEY" ]; then
    SSH_OPTS+=(-i "$REMOTE_KEY" -o IdentitiesOnly=yes)
else
    echo "note: $REMOTE_KEY not found, falling back to the default ssh login" >&2
fi

REMOTE="$REMOTE_USER@$REMOTE_HOST"

rssh() { ssh "${SSH_OPTS[@]}" "$REMOTE" "$@"; }
rscp() { scp "${SSH_OPTS[@]}" "$@"; }

remote_open() {
    echo "==> connecting to $REMOTE (enter the password if asked)"
    rssh true
    trap remote_close EXIT
}

remote_close() { ssh "${SSH_OPTS[@]}" -O exit "$REMOTE" 2>/dev/null || true; }

# rbash <args...>: run the script on stdin on the server with bash, as $1...
rbash() { rssh bash -s -- "$@"; }

# the homebrew libraries the server binaries were linked against. The server
# needs these exact files, so a brew with a newer soname would not work.
local_brew_libs() {
    otool -L "$ROOT/Server/build-default/rrolf-server" | awk '/\/opt\/homebrew\//{print $1}'
}

# remote_check_libs: print any of those libraries missing on the server
remote_check_libs() {
    # shellcheck disable=SC2046
    rbash $(local_brew_libs) <<'EOF'
for f in "$@"; do [ -f "$f" ] || echo "$f"; done
EOF
}
