#!/bin/bash
# Build script for rrolf (Raspberry Pi / Debian-based arm/arm64)
#
# This is the Raspberry Pi counterpart to build.sh (which is macOS-only).
# It builds:
#   - the server (native binary)
#   - the client (as a WASM/JS bundle, via emscripten) - only if emsdk is
#     installed (see setup-pi.sh --with-client); otherwise it's skipped,
#     since building the client on a Pi is slow. Build it on a dev machine
#     and copy Client/build over instead, or run setup-pi.sh --with-client
#     first if you really want to build it on-device.
# and installs the npm dependencies for the root project and MasterServer.
#
# Run ./setup-pi.sh first to install the required apt packages / Node.js.
#
# Env vars:
#   SKIP_CLIENT=1   force-skip the client build even if emsdk is installed
#   SKIP_CLIENT=0   force-attempt the client build (fails if emcc isn't found)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# ---------------------------------------------------------------------------
# Dependency checks - installing them is setup-pi.sh's job, this just makes
# sure that's actually been run before doing anything expensive.
# ---------------------------------------------------------------------------
missing=()
for cmd in cmake make git curl python3 node npm; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
done
if ! command -v clang >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
    missing+=("clang/gcc")
fi
if [ "${#missing[@]}" -gt 0 ]; then
    echo "Missing dependencies: ${missing[*]}" >&2
    echo "Run ./setup-pi.sh first." >&2
    exit 1
fi

# pick up emsdk if setup-pi.sh --with-client installed it
if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "$HOME/emsdk/emsdk_env.sh" >/dev/null
fi

NPROC="$(nproc)"

# ---------------------------------------------------------------------------
# Root npm packages
# ---------------------------------------------------------------------------
echo "==> Installing root npm packages"
npm i

# ---------------------------------------------------------------------------
# Build the client (WASM) - only if emsdk is available
# ---------------------------------------------------------------------------
skip_client="${SKIP_CLIENT:-}"
if [ -z "$skip_client" ]; then
    if command -v emcc >/dev/null 2>&1; then
        skip_client=0
    else
        skip_client=1
    fi
fi

if [ "$skip_client" = "1" ]; then
    if command -v emcc >/dev/null 2>&1; then
        echo "==> Skipping client build (SKIP_CLIENT=1)"
    else
        echo "==> Skipping client build (emscripten not installed - run"
        echo "    ./setup-pi.sh --with-client to build the client on this Pi,"
        echo "    or build it elsewhere and copy Client/build here)"
    fi
else
    if ! command -v emcc >/dev/null 2>&1; then
        echo "SKIP_CLIENT=0 but emcc was not found. Run ./setup-pi.sh --with-client first." >&2
        exit 1
    fi
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
