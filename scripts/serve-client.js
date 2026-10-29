// Static file server for Client/public (the game page plus the wasm build).
// Unlike http-server or python it sets the CSP that restricts who can embed
// the page, so only CrazyGames can iframe it.
//
//   node scripts/serve-client.js            # port 8080
//   PORT=9000 node scripts/serve-client.js
//   CLIENT_DIR=/some/built/files node scripts/serve-client.js

const http = require("http");
const fs = require("fs");
const path = require("path");

const port = Number(process.env.PORT) || 8080;
const root = path.resolve(process.env.CLIENT_DIR ||
                          path.join(__dirname, "..", "Client", "public"));
const frameAncestors = process.env.FRAME_ANCESTORS ||
    "'self' *.crazygames.com https://app.crazygames.com capacitor://app.crazygames.com";

const types = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".wasm": "application/wasm",
    ".png": "image/png",
    ".ttf": "font/ttf",
    ".json": "application/json",
    ".txt": "text/plain; charset=utf-8",
};

// Hand-off for the discord login popup. The popup lands on this host, posts
// the oauth code here under its (secret, random) state, and the game window,
// which may be on any other site, polls for it. A code is handed out once and
// expires after five minutes.
const relay = new Map();
const relayPath = /^\/oauth\/(p\.[0-9a-f-]{36})$/;
const relayHeaders = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Cache-Control": "no-store",
};

function handleRelay(req, res, state) {
    const now = Date.now();
    for (const [key, entry] of relay)
        if (entry.expires < now)
            relay.delete(key);
    if (req.method === "OPTIONS") {
        res.writeHead(204, relayHeaders).end();
    } else if (req.method === "GET") {
        const entry = relay.get(state);
        if (!entry) {
            res.writeHead(204, relayHeaders).end(); // not there yet
            return;
        }
        relay.delete(state);
        res.writeHead(200, { ...relayHeaders, "Content-Type": "text/plain" }).end(entry.code);
    } else if (req.method === "POST") {
        let body = "";
        req.on("data", chunk => {
            if (body.length < 512)
                body += chunk;
        });
        req.on("end", () => {
            if (relay.size >= 1000 || body.length >= 512) {
                res.writeHead(503, relayHeaders).end();
                return;
            }
            relay.set(state, { code: body, expires: Date.now() + 5 * 60 * 1000 });
            res.writeHead(204, relayHeaders).end();
        });
    } else {
        res.writeHead(405, relayHeaders).end();
    }
}

http.createServer((req, res) => {
    const pathname = decodeURIComponent(new URL(req.url, "http://x").pathname);
    const relayMatch = relayPath.exec(pathname);
    if (relayMatch) {
        handleRelay(req, res, relayMatch[1]);
        return;
    }
    let file = path.join(root, pathname === "/" ? "index.html" : pathname);
    if (file !== root && !file.startsWith(root + path.sep)) {
        res.writeHead(403).end("403\n");
        return;
    }
    fs.readFile(file, (err, data) => {
        if (err) {
            res.writeHead(404).end("404 Not Found\n");
            return;
        }
        res.writeHead(200, {
            "Content-Type": types[path.extname(file)] || "application/octet-stream",
            "Cache-Control": "no-cache",
            "Content-Security-Policy": `frame-ancestors ${frameAncestors}`,
        });
        res.end(data);
    });
}).listen(port, () => console.log(`serving ${root} on http://localhost:${port}`));
