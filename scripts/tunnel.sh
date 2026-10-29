#!/bin/bash
# Runs the cloudflared tunnel created by scripts/tunnel-setup.sh.
#
# Moving to another Mac: brew install cloudflared, copy $TUNNEL_CONFIG and the
# <tunnel id>.json credentials file into ~/.cloudflared there (fix the
# credentials-file path in the yml), stop the tunnel here, then run this.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

[ -f "$TUNNEL_CONFIG" ] || { echo "missing $TUNNEL_CONFIG, run scripts/tunnel-setup.sh" >&2; exit 1; }
check_tunnel_config || exit 1
exec cloudflared tunnel --config "$TUNNEL_CONFIG" run "$TUNNEL_NAME"
