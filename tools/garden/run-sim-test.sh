#!/bin/bash
# Runs tools/garden/sim-test.c: the garden's mobs on a simulation, with no
# network. Needs nothing to be free, takes a few seconds.
#
# Usage: tools/garden/run-sim-test.sh [work dir]

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${1:-$(mktemp -d)}"
mkdir -p "$WORK"

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
export CPATH="$BREW_PREFIX/opt/curl/include:$BREW_PREFIX/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$BREW_PREFIX/opt/curl/lib:$BREW_PREFIX/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"

(cd "$WORK" && cmake "$ROOT/Server" -DDEBUG_BUILD=0 -DGARDEN=1 -DGARDEN_SIM_TEST=1 > cmake.log 2>&1 &&
    make -j"$(sysctl -n hw.ncpu)" garden-sim-test > make.log 2>&1) ||
    { echo "build failed, see $WORK/make.log" >&2; exit 2; }
"$WORK/garden-sim-test" 2> "$WORK/test.log"
status=$?
echo "log: $WORK/test.log"
exit $status
