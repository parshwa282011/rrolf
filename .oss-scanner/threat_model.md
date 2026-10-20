# Threat model

## What this project does and where untrusted input enters
Rysteria (rrolf) is a multiplayer browser game. Everything a player sends over the network is untrusted.

- **Game server** (`Server/`, `Shared/`): a C server built on libwebsockets. It listens for WebSocket connections from game clients (port per biome, see `RR_BIOME_SERVERS` in `Shared/StaticData.c`) and parses a custom binary protocol (`Shared/Binary.c`, `Server/Client.c`, `Server/UpdateProtocol.c`, `Shared/pb.c`). Every byte of a client packet is attacker-controlled. It also makes an outbound WebSocket connection to the master server and, in Rivet builds, outbound HTTP calls (`Shared/Rivet.c`).
- **Master server** (`MasterServer/`): a Node.js (Express + `ws`) service on port 55554. It exposes an HTTP API under `/api` and a WebSocket endpoint used by both game clients and game servers. It handles account login/registration, Discord OAuth, CrazyGames token verification and leaderboard submission, and stores accounts in `MasterServer/database.json`. All HTTP and WebSocket input is untrusted.
- **Client** (`Client/`): C compiled to WebAssembly and run in the player's browser. It is not built in this image. It parses data from the server, but the server is trusted by the client, so client-side bugs are low priority.

## Components that matter most / least
- Most: the game server's packet parsing and the code reachable from it, then the master server's authentication, account storage and WebSocket message handling.
- Also in scope: anything that lets one player affect another player's account, inventory, or progress.
- Least: `Client/`, `MapEditor/`, `RivetStaticPage/`, `Balancer/`, build/deploy scripts (`scripts/`, `*.sh`, `nginx.conf`, `nftables.conf`, `crontab`).
- Out of scope: the bundled third-party code `Shared/cJSON.c` and `Shared/cJSON.h`, and `MasterServer/node_modules`.

## How to exercise it
- Game server: `/src/Server/build-asan/rrolf-server` (debug, ASan+UBSan) or `/src/Server/build-release/rrolf-server` (as deployed). It starts without any Rivet or master-server credentials; connect a WebSocket client to the biome port and send crafted binary packets. The handshake and packet formats are in `Server/Client.c` and `Shared/Binary.c`.
- Master server: `cd /src/MasterServer && node main.js`. Secrets (`CLIENT_SECRET`, `BOT_TOKEN`, `PASSWORD_SALT`) are read from the environment or a `.env` file; the server warns but still starts without them. There is no automated test suite.

## How you rate severity
- Remote memory corruption in the game server reachable by an unauthenticated WebSocket client (out-of-bounds read/write, use-after-free, double free, stack or heap overflow): critical if controllable enough to plausibly give code execution, otherwise high.
- A remotely triggerable crash, assertion failure or hang of the game server (kills every player in that lobby): medium, or high if it takes one tiny packet and no state.
- Master server: authentication bypass, account takeover, reading other users' data, or remote code execution: critical. Injection or path traversal: high. Unbounded resource use triggerable by a single client: medium.
- Gameplay-only issues (cheating, duplicating items or XP, speed hacks) that do not touch memory safety or other users' accounts: low.
- Issues that require a malicious server (e.g. attacks on the client from the game server): low.

## Anything to leave alone
- Do not report missing hardening (CORS `*`, missing rate limits, missing HTTP headers) without a concrete exploit.
- Do not report compiler warnings or style issues, or the use of `-ffast-math`.
