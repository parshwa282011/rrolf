// End-to-end test for the C master. Needs a running master and the `ws`
// package (rrolf/MasterServer/node_modules has it).
//   node tests/e2e.js [http://127.0.0.1:55554] [server-secret]
// Creates uniquely named accounts; use a throwaway database.

const path = require("path");
const WebSocket = require(path.join(__dirname, "../../node_modules/ws"));

const BASE = process.argv[2] || "http://127.0.0.1:55554";
const SECRET = process.argv[3] || process.env.SERVER_SECRET || "";
const ORIGIN = "http://localhost:8080";
let failures = 0;

function check(cond, name, extra)
{
    if (cond)
        console.log("  ok   " + name);
    else
    {
        failures++;
        console.log("  FAIL " + name + (extra !== undefined ? "  -> " + JSON.stringify(extra) : ""));
    }
}

async function call(method, url, body, headers = {})
{
    const res = await fetch(BASE + url, {
        method,
        headers: { "content-type": "application/json", ...headers },
        body: body === undefined ? undefined : JSON.stringify(body),
        redirect: "manual",
    });
    let json = null;
    const text = await res.text();
    try { json = JSON.parse(text); } catch (e) {}
    return { status: res.status, json, headers: res.headers, text };
}
// each request pretends to come from a fresh client address (the master trusts
// CF-Connecting-IP from loopback) so the per-IP limits do not interfere
const fakeIp = () => `10.${Math.floor(Math.random() * 250)}.${Math.floor(Math.random() * 250)}.${Math.floor(Math.random() * 250)}`;
const pub = (m, u, b, ip) => call(m, u, b, { origin: ORIGIN, "cf-connecting-ip": ip || fakeIp() });
const srv = (m, u, b) => call(m, u, b, { "x-server-secret": SECRET });

// ---- binary protocol helpers (match Shared/Binary.c) ----
class W
{
    constructor() { this.b = []; }
    u8(v) { this.b.push(v & 255); return this; }
    str(s) { for (const c of Buffer.from(s)) this.b.push(c); this.b.push(0); return this; }
    f64(v) { const x = Buffer.alloc(8); x.writeDoubleLE(v); for (const c of x) this.b.push(c); return this; }
    varuint(v)
    {
        v = BigInt(v);
        while (v > 127n) { this.b.push(Number(((v & 127n) << 1n) | 1n)); v >>= 7n; }
        this.b.push(Number(v << 1n));
        return this;
    }
    buf() { return Buffer.from(this.b); }
}
class R
{
    constructor(b) { this.b = b; this.at = 0; }
    u8() { return this.b[this.at++]; }
    str() { let s = this.at; while (this.b[this.at]) this.at++; const r = this.b.toString("utf8", s, this.at); this.at++; return r; }
    f64() { const v = this.b.readDoubleLE(this.at); this.at += 8; return v; }
    varuint()
    {
        let data = 0n, shift = 0n, byte;
        do { byte = this.u8(); data |= (BigInt(byte & 254) << shift) >> 1n; shift += 7n; } while (byte & 1);
        return Number(data);
    }
    counts()
    {
        const out = {};
        let id;
        while ((id = this.u8())) { const r = this.u8(); out[id + ":" + r] = this.varuint(); }
        return out;
    }
    loadout()
    {
        const out = {};
        let slot;
        while ((slot = this.u8())) out[slot] = [this.u8(), this.u8()];
        return out;
    }
}

function connectGame(useHeader)
{
    return new Promise((resolve, reject) => {
        const url = BASE.replace("http", "ws") + (useHeader ? "/" : "/api/" + SECRET);
        const ws = new WebSocket(url, "g", useHeader ? { headers: { "x-server-secret": SECRET } } : {});
        const g = { ws, queue: [], waiters: [] };
        ws.on("message", (m) => {
            const buf = Buffer.from(m);
            const w = g.waiters.shift();
            if (w) w(buf); else g.queue.push(buf);
        });
        g.next = (ms = 3000) => new Promise((res, rej) => {
            if (g.queue.length) return res(g.queue.shift());
            const t = setTimeout(() => rej(new Error("timeout waiting for message")), ms);
            g.waiters.push((b) => { clearTimeout(t); res(b); });
        });
        ws.on("open", async () => {
            try
            {
                const hello = new R(await g.next());
                check(hello.u8() === 45 && hello.u8() === 0, "ws hello");
                g.alias = hello.str();
                resolve(g);
            }
            catch (e) { reject(e); }
        });
        ws.on("error", reject);
        ws.on("unexpected-response", (req, res) => reject(new Error("rejected " + res.statusCode)));
    });
}

// what the game server sends: uuid, token (the PL-P), code, nonce, slot
function join(g, uuid, pos, plp, nonce)
{
    g.ws.send(new W().u8(0).str(uuid).str(plp).str("").varuint(nonce).u8(pos).buf());
}

// the join reply up to and including the dev flag
function readJoinHead(m, pos, nonce, uuid, token, name)
{
    return m.u8() === 45 && m.u8() === 1 && m.u8() === pos && m.varuint() === nonce &&
        m.str() === uuid && m.str() === token && m.str() === name && m.str() === "" && m.str() === "";
}

(async () => {
    const tag = Math.floor(Math.random() * 1e6);
    const name = "E2E_" + tag, email = `e2e_${tag}@example.com`, pw = "correct horse battery";

    console.log("http: health, cors");
    let r = await call("GET", "/health");
    check(r.status === 200 && r.json.ok, "health", r);
    r = await call("OPTIONS", "/auth/login", undefined, { origin: ORIGIN, "access-control-request-method": "POST" });
    check(r.status === 204 && r.headers.get("access-control-allow-origin") === ORIGIN, "preflight allowed origin", r.status);
    r = await call("OPTIONS", "/auth/login", undefined, { origin: "https://evil.example" });
    check(r.status === 403 && !r.headers.get("access-control-allow-origin"), "preflight foreign origin refused", r.status);
    r = await call("POST", "/auth/login", { identifier: "x", password: "y" }, { origin: "https://evil.example" });
    check(r.status === 403, "foreign origin POST refused", r.status);
    r = await pub("GET", "/nope");
    check(r.status === 404, "404");

    console.log("http: signup / login / session");
    r = await pub("POST", "/auth/signup", { email: "bad", username: name, password: pw });
    check(r.status === 400 && r.json.error === "invalid_email", "signup bad email", r.json);
    r = await pub("POST", "/auth/signup", { email, username: "x", password: pw });
    check(r.status === 400 && r.json.error === "invalid_username", "signup bad username", r.json);
    r = await pub("POST", "/auth/signup", { email, username: name, password: "short" });
    check(r.status === 400 && r.json.error === "invalid_password", "signup short password", r.json);
    r = await pub("POST", "/auth/signup", { email, username: name, password: pw });
    check(r.status === 201 && r.json.uuid && r.json.pl_t && r.json.pl_p, "signup", r);
    const acct = r.json;
    check(r.headers.get("access-control-allow-origin") === ORIGIN, "cors header on response");
    r = await pub("POST", "/auth/signup", { email: "other_" + email, username: name.toLowerCase(), password: pw });
    check(r.status === 409 && r.json.error === "username_taken", "duplicate username (case-insensitive)", r.json);
    r = await pub("POST", "/auth/signup", { email: email.toUpperCase(), username: name + "x", password: pw });
    check(r.status === 409 && r.json.error === "email_taken", "duplicate email (case-insensitive)", r.json);

    r = await pub("POST", "/auth/login", { identifier: name, password: "wrong password" });
    check(r.status === 401 && r.json.error === "invalid_credentials", "login wrong password", r.json);
    r = await pub("POST", "/auth/login", { identifier: "nobody_" + tag, password: pw });
    check(r.status === 401 && r.json.error === "invalid_credentials", "login unknown user same error", r.json);
    r = await pub("POST", "/auth/login", { identifier: email, password: pw });
    check(r.status === 200 && r.json.uuid === acct.uuid, "login by email", r.json);
    const login = r.json;
    r = await pub("POST", "/auth/login", { identifier: name.toUpperCase(), password: pw });
    check(r.status === 200 && r.json.uuid === acct.uuid, "login by username (case-insensitive)");
    check(login.pl_t === acct.pl_t, "PL-T is stable across logins");
    check(login.pl_p !== acct.pl_p, "PL-P differs per login");

    r = await pub("POST", "/auth/session", { uuid: acct.uuid, pl_t: login.pl_t, pl_p: login.pl_p });
    check(r.status === 200 && r.json.ok, "session ok", r.json);
    r = await pub("POST", "/auth/session", { uuid: acct.uuid, pl_t: login.pl_t, pl_p: login.pl_p.slice(0, -2) + "AA" });
    check(r.status === 401, "session rejects tampered PL-P", r.json);
    r = await pub("POST", "/auth/session", { uuid: acct.uuid, pl_t: "x".repeat(43), pl_p: login.pl_p });
    check(r.status === 401, "session rejects wrong PL-T", r.json);
    r = await pub("POST", "/auth/session", { uuid: acct.uuid, pl_p: login.pl_p });
    check(r.status === 401, "session requires PL-T", r.json);
    r = await pub("POST", "/auth/account", { uuid: acct.uuid, pl_t: login.pl_t, pl_p: login.pl_p });
    check(r.status === 200 && r.json.email === email && Array.isArray(r.json.links), "account info", r.json);

    console.log("http: unavailable providers, reset");
    for (const p of ["/oauth/google/start", "/oauth/apple/start"])
    {
        r = await pub("GET", p);
        check(r.status === 503 && r.json.error === "provider_unavailable", p + " unavailable");
    }
    r = await pub("POST", "/auth/email/send", {});
    check(r.status === 503 && r.json.error === "provider_unavailable", "email unavailable");
    r = await pub("GET", "/oauth/discord/start");
    check(r.status === 302 && /oauth_error=unavailable/.test(r.headers.get("location") || ""), "discord start unavailable without credentials", r.headers.get("location"));
    r = await pub("POST", "/auth/reset/start", { identifier: name });
    check(r.status === 200 && r.json.reset_id && r.json.methods.some((m) => m.method === "email" && !m.available), "reset start lists email unavailable", r.json);
    const resetId = r.json.reset_id;
    r = await pub("POST", "/auth/reset/send", { reset_id: resetId, method: "email" });
    check(r.status === 503, "reset via email unavailable", r.json);
    r = await pub("POST", "/auth/reset/send", { reset_id: resetId, method: "discord" });
    check(r.status === 400 && r.json.error === "method_unavailable", "reset via unlinked discord refused", r.json);
    r = await pub("POST", "/auth/reset/confirm", { reset_id: resetId, otp: "123456", new_password: "a brand new password" });
    check(r.status === 400, "reset confirm without code refused", r.json);

    console.log("http: rate limits");
    const sameIp = fakeIp();
    const codes = [];
    for (let i = 0; i < 7; i++)
        codes.push((await pub("POST", "/auth/signup", { email: "bad", username: "x", password: "y" }, sameIp)).status);
    check(codes.slice(0, 5).every((c) => c === 400) && codes[5] === 429, "signup limited to 5/hour per IP", codes);
    codes.length = 0;
    for (let i = 0; i < 12; i++)
        codes.push((await pub("POST", "/auth/login", { identifier: "nobody_" + i, password: "whatever12" }, sameIp)).status);
    check(codes[9] === 401 && codes[10] === 429, "login limited to 10/10min per IP", codes);

    console.log("http: lockout");
    const lockName = "Lock_" + tag;
    await pub("POST", "/auth/signup", { email: `lock_${tag}@example.com`, username: lockName, password: pw });
    for (let i = 0; i < 5; i++)
        await pub("POST", "/auth/login", { identifier: lockName, password: "wrong wrong wrong" });
    r = await pub("POST", "/auth/login", { identifier: lockName, password: pw });
    check(r.status === 429 && r.json.error === "locked", "account locks after 5 failures", r.json);

    console.log("http: server routes");
    r = await call("POST", "/srv/verify", { uuid: acct.uuid, pl_p: login.pl_p });
    check(r.status === 403, "srv without secret refused", r.status);
    r = await call("POST", "/srv/verify", { uuid: acct.uuid, pl_p: login.pl_p }, { "x-server-secret": SECRET, origin: ORIGIN });
    check(r.status === 403, "srv from a browser origin refused", r.status);
    r = await srv("POST", "/srv/verify", { uuid: acct.uuid, pl_p: login.pl_p });
    check(r.status === 200 && r.json.username === name && r.json.is_dev === false, "srv verify", r.json);
    r = await srv("POST", "/srv/verify", { uuid: acct.uuid, pl_p: "garbage" });
    check(r.status === 401, "srv verify bad token", r.json);

    const s1 = { save_id: "aa" + tag, score: 5000, loadout: [1, 2, 3, 4] };
    const s2 = { save_id: "bb" + tag, score: 90000, loadout: [5, 6, 7] };
    r = await srv("POST", "/srv/gardn/flush", { changes: [{ uuid: acct.uuid, add: [s1, s2] }] });
    check(r.status === 200 && r.json.ok && r.json.applied === 1, "gardn flush add", r.json);
    r = await srv("POST", "/srv/gardn/flush", { changes: [{ uuid: acct.uuid, add: [s1, s2] }] });
    check(r.json.ok, "gardn flush is idempotent", r.json);
    r = await srv("POST", "/srv/gardn/load", { uuid: acct.uuid, pl_p: login.pl_p });
    check(r.status === 200 && r.json.saves.length === 2, "gardn load", r.json);
    r = await srv("POST", "/srv/gardn/flush", { changes: [{ uuid: acct.uuid, remove: [s1.save_id] }] });
    r = await srv("POST", "/srv/gardn/load", { uuid: acct.uuid, pl_p: login.pl_p });
    check(r.json.saves.length === 1 && r.json.saves[0].save_id === s2.save_id && r.json.saves[0].score === 90000, "gardn remove", r.json);
    r = await srv("POST", "/srv/gardn/flush", { changes: [{ uuid: acct.uuid, add: [{ save_id: "../bad", score: 1, loadout: [] }] }] });
    check(r.json.ok === false && r.json.failed.length === 1, "gardn flush rejects bad save id", r.json);
    r = await srv("POST", "/srv/leaderboard/submit", { board: "e2e", uuid: acct.uuid, score: 42, extra: { a: 1 } });
    check(r.status === 200, "leaderboard submit");
    r = await srv("GET", "/srv/leaderboard/top?board=e2e&limit=5");
    check(r.status === 200 && r.json.entries[0].score === 42, "leaderboard top", r.json);

    console.log("ws: game server link");
    let denied;
    try { await connectGame(false).then((g) => g.ws.close()); denied = false; } catch (e) { denied = true; }
    check(!denied, "ws accepted with secret in path");
    const g = await connectGame(true);
    check(/^[0-9a-z]+$/.test(g.alias), "ws accepted with secret header, alias " + g.alias);
    let rejected = false;
    try { await new Promise((res, rej) => { const w = new WebSocket(BASE.replace("http", "ws") + "/", "g"); w.on("open", () => res()); w.on("error", rej); w.on("unexpected-response", (q, s) => rej(new Error("rejected"))); }); } catch (e) { rejected = true; }
    check(rejected, "ws without secret rejected");

    join(g, acct.uuid, 3, "garbage", 777);
    let m = new R(await g.next());
    check(m.u8() === 45 && m.u8() === 2 && m.u8() === 3 && m.varuint() === 777, "join with bad PL-P is kicked (slot + nonce)");

    join(g, acct.uuid, 3, login.pl_p, 1001);
    m = new R(await g.next());
    check(readJoinHead(m, 3, 1001, acct.uuid, login.pl_p, name), "join ok, nonce/uuid/token/username echoed");
    check(m.u8() === 0, "not dev");
    check(m.f64() === 0 && m.varuint() === 0 && m.u8() === 0 && m.u8() === 0 && m.u8() === 0, "new account xp/pvp points/checkpoints");
    const petals = m.counts();
    check(petals["1:0"] === 5, "new account starts with 5 basic petals", petals);
    m.counts();
    check(Object.keys(m.counts()).length === 0 && Object.keys(m.loadout()).length === 0 && m.str() === "", "join tail empty");

    // save: xp beyond 2^31, a count beyond 2^31, tail data
    const big = 3000000000;
    g.ws.send(new W().u8(2).str(acct.uuid).f64(1.5e22).varuint(5000000000).u8(2).u8(1).u8(3)
        .u8(1).u8(0).varuint(7).u8(27).u8(6).varuint(big).u8(0)
        .u8(3).u8(1).varuint(4).u8(0)
        .u8(5).u8(2).varuint(11).u8(0)
        .u8(1).u8(9).u8(4).u8(2).u8(10).u8(5).u8(0)
        .str("Nicky").buf());
    await new Promise((res) => setTimeout(res, 300));

    // second connection takes the account over: first must be kicked
    const g2 = await connectGame(true);
    join(g2, acct.uuid, 9, login.pl_p, 2002);
    m = new R(await g.next());
    check(m.u8() === 45 && m.u8() === 2 && m.u8() === 3 && m.varuint() === 1001, "duplicate login kicks the old session (its nonce)");
    g.ws.send(new W().u8(1).str(acct.uuid).u8(3).buf()); // old session leaves
    m = new R(await g2.next(6000));
    check(readJoinHead(m, 9, 2002, acct.uuid, login.pl_p, name), "new session accepted after old one left");
    m.u8();
    check(m.f64() === 1.5e22 && m.varuint() === 5000000000 && m.u8() === 2 && m.u8() === 1 && m.u8() === 3, "takeover carries xp + pvp points + 3 checkpoints");
    const p2 = m.counts();
    check(p2["1:0"] === 7 && p2["27:6"] === big, "petals incl. count > 2^31", p2);
    check(m.counts()["3:1"] === 4, "failed crafts");
    check(m.counts()["5:2"] === 11, "mob kills");
    const lo = m.loadout();
    check(lo[1][0] === 9 && lo[1][1] === 4 && lo[2][0] === 10, "loadout", lo);
    check(m.str() === "Nicky", "nickname");
    g.ws.close();

    // leave, wait for the flush, then restart-like check through a fresh join
    g2.ws.send(new W().u8(1).str(acct.uuid).u8(9).buf());
    await new Promise((res) => setTimeout(res, 2500));
    const g3 = await connectGame(true);
    join(g3, acct.uuid, 0, login.pl_p, 3003);
    m = new R(await g3.next());
    check(readJoinHead(m, 0, 3003, acct.uuid, login.pl_p, name), "rejoin after leave");
    m.u8();
    check(m.f64() === 1.5e22 && m.varuint() === 5000000000, "data persisted in the database after leave");
    g3.ws.send(new W().u8(1).str(acct.uuid).u8(0).buf());

    // logout everywhere kills both tokens
    r = await pub("POST", "/auth/logout-all", { uuid: acct.uuid, pl_t: login.pl_t, pl_p: login.pl_p });
    check(r.status === 200, "logout-all", r.json);
    r = await pub("POST", "/auth/session", { uuid: acct.uuid, pl_t: login.pl_t, pl_p: login.pl_p });
    check(r.status === 401, "tokens dead after logout-all", r.json);
    r = await pub("POST", "/auth/login", { identifier: name, password: pw });
    check(r.status === 200 && r.json.pl_t !== login.pl_t, "login still works, new PL-T issued", r.json);
    g2.ws.close(); g3.ws.close();

    // op 5: the old in-game account requests are answered "not available"
    const g4 = await connectGame(true);
    g4.ws.send(new W().u8(5).u8(2).varuint(4004).u8(1).u8(0).str("").buf());
    m = new R(await g4.next());
    check(m.u8() === 45 && m.u8() === 3 && m.u8() === 2 && m.varuint() === 4004 && m.u8() === 0 && m.u8() === 1,
        "account request answered with ok = 0");
    g4.ws.close();

    console.log(failures ? `\n${failures} FAILED` : "\nall e2e checks passed");
    process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
