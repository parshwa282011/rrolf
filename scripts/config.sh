# Shared settings for the scripts in this folder. Sourced, not executed.
# Every value can be overridden from the environment, e.g. DOMAIN=example.com.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Public hostnames served through the cloudflared tunnel.
DOMAIN="${DOMAIN:-parshwa.blog}"
HOST_WEB="${HOST_WEB:-game.$DOMAIN}"
HOST_DEFAULT="${HOST_DEFAULT:-default-game.$DOMAIN}"
HOST_PVP="${HOST_PVP:-pvp-game.$DOMAIN}"
HOST_GARDEN="${HOST_GARDEN:-garden-game.$DOMAIN}"
HOST_CTF="${HOST_CTF:-ctf-game.$DOMAIN}"
# There is deliberately no hostname for the MasterServer. It only listens on
# loopback and the game servers talk to it; players link accounts and use
# transfer codes through their game server.

# Local ports. The game server ports are fixed by RR_BIOME_SERVERS in
# Shared/StaticData.c, so only change them there too.
PORT_WEB="${PORT_WEB:-8080}"
PORT_DEFAULT=1234
PORT_PVP=1235
PORT_GARDEN=1236
PORT_CTF=1237
PORT_MASTER=55554

# cloudflared. Moving to another Mac: copy the config and the credentials json
# next to it, fix the credentials path, then run scripts/tunnel.sh there.
TUNNEL_NAME="${TUNNEL_NAME:-rysteria-cg}"
TUNNEL_CONFIG="${TUNNEL_CONFIG:-$HOME/.cloudflared/$TUNNEL_NAME.yml}"

# check_tunnel_config: stop if the tunnel config would publish the MasterServer
# (anything pointing at its port) or still has the old api hostname.
check_tunnel_config() {
    if grep -Eq "(localhost|127\.0\.0\.1|\[::1\]):$PORT_MASTER([^0-9]|\$)" "$TUNNEL_CONFIG"; then
        echo "$TUNNEL_CONFIG exposes the MasterServer (port $PORT_MASTER) through the tunnel." >&2
        echo "Run scripts/tunnel-setup.sh --config-only to write it without." >&2
        return 1
    fi
}
