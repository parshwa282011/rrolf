#!/bin/bash
# One-time setup of the server Mac: checks it matches this machine and
# installs what the stack needs through Homebrew (libwebsockets, curl, node,
# cloudflared). Safe to run again.
#
# Homebrew itself is not installed for you, its installer needs sudo and a
# terminal. Install it by hand first: https://brew.sh
#
# Usage: scripts/remote-setup.sh

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/remote.sh"
cd "$ROOT"

[ -f Server/build-default/rrolf-server ] || { echo "build first: scripts/build.sh" >&2; exit 1; }

remote_open

echo "==> checking the server"
remote_arch="$(rssh uname -m)"
if [ "$remote_arch" != "$(uname -m)" ]; then
    echo "the server is $remote_arch but the binaries here are $(uname -m), they will not run there" >&2
    exit 1
fi

rbash <<'EOF'
set -e
[ "$(uname -s)" = Darwin ] || { echo "the server is not macOS" >&2; exit 1; }
[ -x /opt/homebrew/bin/brew ] || { echo "Homebrew is not installed in /opt/homebrew, see https://brew.sh" >&2; exit 1; }
export PATH=/opt/homebrew/bin:$PATH
for formula in libwebsockets curl node cloudflared; do
    if brew list --formula "$formula" > /dev/null 2>&1; then
        echo "have $formula"
    else
        echo "installing $formula"
        brew install "$formula"
    fi
done
EOF

missing="$(remote_check_libs)"
if [ -n "$missing" ]; then
    echo "the server is missing libraries the binaries were linked against:" >&2
    echo "$missing" >&2
    echo "its Homebrew has a different version than this Mac. Run 'brew upgrade' on both, rebuild, and retry." >&2
    exit 1
fi

echo
echo "server is ready. Next: scripts/remote-deploy.sh"
echo "Not covered: the stack does not restart after a reboot, and the Mac must not be shut down."
