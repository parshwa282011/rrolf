#!/bin/bash
# Build script for rrolf (macOS)
#
# This is a macOS translation of the Debian instructions in README.md.
# It builds:
#   - the client (as a WASM/JS bundle, via emscripten)
#   - the server (native binary)
# and installs the npm dependencies for the root project and MasterServer.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# ---------------------------------------------------------------------------
# Dependency checks / install (macOS uses Homebrew in place of apt)
# ---------------------------------------------------------------------------
if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required. Install it from https://brew.sh and re-run this script." >&2
    exit 1
fi

BREW_PREFIX="$(brew --prefix)"

ensure_formula() {
    local formula="$1"
    if ! brew list --formula "$formula" >/dev/null 2>&1; then
        echo "==> Installing $formula via Homebrew"
        brew install "$formula"
    fi
}

ensure_formula cmake
ensure_formula libwebsockets
ensure_formula curl
ensure_formula emscripten
ensure_formula node

if ! command -v java >/dev/null 2>&1; then
    echo "==> Installing openjdk (needed by emscripten's closure compiler)"
    ensure_formula openjdk
    export PATH="$BREW_PREFIX/opt/openjdk/bin:$PATH"
fi

# Homebrew on Apple Silicon (/opt/homebrew) is not on the default clang
# search path the way Linux's /usr is, so point the compiler at it explicitly.
export CPATH="$BREW_PREFIX/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$BREW_PREFIX/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# curl is keg-only on Homebrew (macOS ships its own); prefer the brewed one.
export CPATH="$BREW_PREFIX/opt/curl/include:$CPATH"
export LIBRARY_PATH="$BREW_PREFIX/opt/curl/lib:$LIBRARY_PATH"

NPROC="$(sysctl -n hw.ncpu)"

# ---------------------------------------------------------------------------
# Root npm packages
# ---------------------------------------------------------------------------
echo "==> Installing root npm packages"
npm i

# ---------------------------------------------------------------------------
# Build the client (WASM)
# ---------------------------------------------------------------------------
if [ "${SKIP_CLIENT:-0}" = "1" ]; then
    echo "==> Skipping client build (SKIP_CLIENT=1)"
else
    echo "==> Building client"
    mkdir -p Client/build
    (
        cd Client/build
        # Newer emscripten releases stopped auto-defining the bare EMSCRIPTEN
        # macro (only __EMSCRIPTEN__), which this codebase's `#ifdef EMSCRIPTEN`
        # guards still rely on. Define it explicitly to restore that behavior.
        cmake .. -DWASM_BUILD=1 -DDEBUG_BUILD=0 -DCMAKE_C_FLAGS="-DEMSCRIPTEN"
        make -j"$NPROC"
        cp ../../RivetStaticPage/index.html .
    )
fi

# ---------------------------------------------------------------------------
# Build the server
# ---------------------------------------------------------------------------
echo "==> Building server"
mkdir -p Server/build
(
    cd Server/build
    cmake .. -DDEBUG_BUILD=0
    make -j"$NPROC"
)

# ---------------------------------------------------------------------------
# MasterServer npm packages
# ---------------------------------------------------------------------------
echo "==> Installing MasterServer npm packages"
(
    cd MasterServer
    npm i
)

echo "==> Build complete"
