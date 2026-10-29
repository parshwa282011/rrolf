#!/bin/bash
# One-time cloudflared setup. Creates the tunnel, points the three hostnames at
# it and writes the tunnel config. Needs `cloudflared tunnel login` to have been
# run once (~/.cloudflared/cert.pem).
#
# The DNS step overwrites existing records for the hostnames, so any older
# tunnel stops serving them. It asks before doing that.
#
# The MasterServer is NOT on the tunnel: players link accounts and use transfer
# codes through their game server. An earlier version published it as
# api-game.$DOMAIN; this config no longer routes that name (it answers 404),
# and its DNS record can be deleted in the Cloudflare dashboard.
#
# Usage: scripts/tunnel-setup.sh [--config-only]
#   --config-only  only (re)write $TUNNEL_CONFIG for the existing tunnel, no
#                  DNS changes and no questions. Use it after updating this
#                  script, then redeploy so the server gets the new config.

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

config_only=0
case "${1:-}" in
"") ;;
--config-only) config_only=1 ;;
*) echo "unknown option $1" >&2; exit 1 ;;
esac

command -v cloudflared >/dev/null || { echo "missing cloudflared (brew install cloudflared)" >&2; exit 1; }
[ -f "$HOME/.cloudflared/cert.pem" ] || { echo "run: cloudflared tunnel login" >&2; exit 1; }

tunnel_id() {
    cloudflared tunnel list --name "$TUNNEL_NAME" --output json |
        node -e 'const t = JSON.parse(require("fs").readFileSync(0, "utf8")); if (t && t.length) console.log(t[0].id)'
}

ID="$(tunnel_id)"
if [ -z "$ID" ]; then
    echo "==> creating tunnel $TUNNEL_NAME"
    cloudflared tunnel create "$TUNNEL_NAME"
    ID="$(tunnel_id)"
else
    echo "==> tunnel $TUNNEL_NAME already exists ($ID)"
fi
[ -f "$HOME/.cloudflared/$ID.json" ] ||
    echo "warning: $HOME/.cloudflared/$ID.json is missing, the tunnel was made on another machine" >&2

if [ "$config_only" = 0 ]; then
    echo
    echo "About to point these hostnames at $TUNNEL_NAME, replacing existing DNS records:"
    printf '  %s\n' "$HOST_WEB" "$HOST_DEFAULT" "$HOST_PVP" "$HOST_GARDEN" "$HOST_CTF"
    read -r -p "Continue? [y/N] " answer
    [ "$answer" = y ] || { echo "aborted"; exit 1; }

    for host in "$HOST_WEB" "$HOST_DEFAULT" "$HOST_PVP" "$HOST_GARDEN" "$HOST_CTF"; do
        cloudflared tunnel route dns --overwrite-dns "$TUNNEL_NAME" "$host"
    done
fi

# Only the web host and the three game servers. Anything else, the MasterServer
# included, falls through to the 404 at the end.
mkdir -p "$(dirname "$TUNNEL_CONFIG")"
[ -f "$TUNNEL_CONFIG" ] && cp "$TUNNEL_CONFIG" "$TUNNEL_CONFIG.bak"
cat > "$TUNNEL_CONFIG" <<EOF
tunnel: $ID
credentials-file: $HOME/.cloudflared/$ID.json

ingress:
  - hostname: $HOST_WEB
    service: http://localhost:$PORT_WEB
  - hostname: $HOST_DEFAULT
    service: http://localhost:1234
  - hostname: $HOST_PVP
    service: http://localhost:1235
  - hostname: $HOST_GARDEN
    service: http://localhost:1236
  - hostname: $HOST_CTF
    service: http://localhost:1237
  - service: http_status:404
EOF

check_tunnel_config

echo
echo "wrote $TUNNEL_CONFIG"
echo "start it with scripts/tunnel.sh, or scripts/dev-up.sh --tunnel"
