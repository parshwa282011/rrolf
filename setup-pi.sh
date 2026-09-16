#!/bin/bash
# Dependency installer for rrolf on a Raspberry Pi (Raspberry Pi OS / any
# Debian-based arm/arm64 distro).
#
# Installs everything build-pi.sh and run-pi.sh need:
#   - build tools: build-essential, clang, cmake, make, git, pkg-config
#   - libwebsockets + libcurl dev headers (server dependencies)
#   - python3 (hosts the client), lsof (used by stop.sh)
#   - Node.js LTS via NodeSource (Raspberry Pi OS's apt Node is often too
#     old for this project's npm packages)
#
# By default this does NOT install emscripten, since building the WASM
# client on a Pi is slow and disk/RAM-hungry. If you actually want to build
# the client on-device (rather than building it elsewhere and copying
# Client/build over), pass --with-client to also install emsdk.
#
# Usage:
#   ./setup-pi.sh                # server-only dependencies (recommended)
#   ./setup-pi.sh --with-client  # also install emscripten for building the client

set -euo pipefail

with_client=0
while [ $# -gt 0 ]; do
    case "$1" in
    --with-client)
        with_client=1
        shift
        ;;
    -h | --help)
        sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        exit 1
        ;;
    esac
done

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script is for Debian-based systems (e.g. Raspberry Pi OS) with apt-get." >&2
    exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "Need root (or sudo) to install packages." >&2
        exit 1
    fi
fi

echo "==> Updating apt package lists"
$SUDO apt-get update

echo "==> Installing build tools and server dependencies"
$SUDO apt-get install -y \
    build-essential \
    clang \
    cmake \
    make \
    git \
    curl \
    ca-certificates \
    pkg-config \
    python3 \
    lsof \
    libwebsockets-dev \
    libcurl4-openssl-dev

# ---------------------------------------------------------------------------
# Node.js (for the MasterServer and root npm packages)
# ---------------------------------------------------------------------------
node_ok=0
if command -v node >/dev/null 2>&1; then
    node_major="$(node -e 'console.log(process.versions.node.split(".")[0])' 2>/dev/null || echo 0)"
    if [ "$node_major" -ge 18 ] 2>/dev/null; then
        node_ok=1
    fi
fi

if [ "$node_ok" = "1" ]; then
    echo "==> Node.js $(node -v) already installed, skipping"
else
    echo "==> Installing Node.js LTS via NodeSource (apt's default is usually too old)"
    curl -fsSL https://deb.nodesource.com/setup_lts.x | $SUDO -E bash -
    $SUDO apt-get install -y nodejs
fi

# ---------------------------------------------------------------------------
# emscripten (only needed to build the WASM client on-device)
# ---------------------------------------------------------------------------
if [ "$with_client" = "1" ]; then
    EMSDK_DIR="$HOME/emsdk"
    if [ -x "$EMSDK_DIR/emsdk" ]; then
        echo "==> emsdk already present at $EMSDK_DIR, updating"
        (cd "$EMSDK_DIR" && git pull)
    else
        echo "==> Installing emsdk to $EMSDK_DIR (this downloads/builds LLVM for your"
        echo "    Pi's architecture and can take a long time and a lot of disk space,"
        echo "    especially on a Pi 3 or older)"
        git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    fi
    (
        cd "$EMSDK_DIR"
        ./emsdk install latest
        ./emsdk activate latest
    )
    echo "==> emsdk installed. build-pi.sh will pick it up automatically."
else
    echo "==> Skipping emscripten (pass --with-client to install it and build the WASM client on this Pi)"
fi

echo
echo "==> All dependencies installed."
echo "    Next: ./run-pi.sh (or ./build-pi.sh to just build)"
