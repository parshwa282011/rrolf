#!/bin/bash
# Stops everything scripts/remote-deploy.sh started on the server: the
# MasterServer (which saves its database first), both game servers, the client
# host, the tunnel and the keep-awake helper. Files are left in place.
#
# Usage: scripts/remote-stop.sh

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/remote.sh"

remote_open
rbash "$REMOTE_DIR" <<'EOF'
dir="$HOME/$1"
if [ -x "$dir/stop.sh" ]; then
    "$dir/stop.sh"
else
    echo "nothing deployed at ~/$1"
fi
EOF
