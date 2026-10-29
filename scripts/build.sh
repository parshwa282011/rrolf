#!/bin/bash
# Builds everything for local development on macOS:
#   - the wasm client into Client/build, copied into Client/public
#   - the hell creek server into Server/build-default (port 1234)
#   - the pvp server into Server/build-pvp (port 1235)
#   - the garden server into Server/build-garden (port 1236)
#   - the capture the flag server into Server/build-ctf (port 1237). It is always
#     built; --ctf-enable only decides whether the deploy and dev scripts run it
#   - the MasterServer npm packages
#
# Usage: scripts/build.sh [--skip-client] [--ctf-enable]
#   --ctf-enable  accepted so that the same command line works for the build, the
#                 deploy and dev-up.sh. It changes nothing here

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"
cd "$ROOT"

skip_client=0
for arg in "$@"; do
    case "$arg" in
    --skip-client) skip_client=1 ;;
    --ctf-enable) ;;
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

for cmd in cmake clang node npm; do
    command -v "$cmd" >/dev/null || { echo "missing $cmd" >&2; exit 1; }
done
[ "$skip_client" = 1 ] || command -v emcc >/dev/null || { echo "missing emcc (brew install emscripten)" >&2; exit 1; }

# Homebrew on Apple Silicon is not on clang's default search path. curl is
# keg-only, so prefer the brewed one over the system copy.
BREW_PREFIX="$(brew --prefix)"
export CPATH="$BREW_PREFIX/opt/curl/include:$BREW_PREFIX/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$BREW_PREFIX/opt/curl/lib:$BREW_PREFIX/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# emscripten's closure compiler needs java.
command -v java >/dev/null || export PATH="$BREW_PREFIX/opt/openjdk/bin:$PATH"

NPROC="$(sysctl -n hw.ncpu)"

if [ "$skip_client" = 0 ]; then
    echo "==> client (wasm)"
    mkdir -p Client/build
    (
        cd Client/build
        # newer emscripten no longer defines the bare EMSCRIPTEN macro that
        # some #ifdefs here still check
        cmake .. -Wimplicit-function-declaration -DWASM_BUILD=1 -DDEBUG_BUILD=0 -DPDEV=1 -DCMAKE_C_FLAGS="-DEMSCRIPTEN" 
        make -j"$NPROC"
        cp rrolf-client.js rrolf-client.wasm ../public/
    )
fi

echo "==> server (hell creek, port $PORT_DEFAULT)"
mkdir -p Server/build-default && (cd Server/build-default && cmake .. -DDEBUG_BUILD=0 -DPVP=0 -DPDEV=1 && make -j"$(sysctl -n hw.ncpu)")
#mkdir -p Server/build-default
#(cd Server/build-default && cmake .. -DDEBUG_BUILD=0 && make -j"$NPROC")

echo "==> server (pvp, port $PORT_PVP)"
mkdir -p Server/build-pvp
(cd Server/build-pvp && cmake .. -DDEBUG_BUILD=0 -DPVP=1 -DPDEV=1 && make -j"$NPROC")

echo "==> server (garden, port $PORT_GARDEN)"
mkdir -p Server/build-garden
(cd Server/build-garden && cmake .. -DDEBUG_BUILD=0 -DGARDEN=1 -DPDEV=1 && make -j"$NPROC")

echo "==> server (capture the flag, port $PORT_CTF)"
mkdir -p Server/build-ctf
# -DPDEV keeps the give petals PIN working in a match. Anyone who knows the PIN
# can use it, so it must not be shared
(cd Server/build-ctf && cmake .. -DDEBUG_BUILD=0 -DCTF=1 -DPDEV=1 && make -j"$NPROC")

echo "==> MasterServer packages"
(cd MasterServer && npm install --no-audit --no-fund)

echo "==> build complete"
