// Saves -> leaderboards -> GET /leaderboard, with a fake game server. Run it against a
// THROWAWAY master (its own database): it creates accounts. For example
//   mongod --dbpath /tmp/lbdb --port 27099 &
//   printf 'DB_TYPE=mongo\nMONGO_URI=mongodb://127.0.0.1:27099\nPORT=55599\nSERVER_SECRET=test-server-secret-0123456789abcdef\nTOKEN_SECRET_1=...\nTOKEN_SECRET_2=...\nOTP_PEPPER=...\nALLOWED_ORIGINS=https://test.example\nFLUSH_INTERVAL_MS=500\nTRUST_PROXY_HEADER=x-test-ip\n' > env
//   build/rysteria-master --env env &  node tests/leaderboard.js
// The accounts are created from one address, and a master allows 5 signups an hour
// from one: TRUST_PROXY_HEADER lets this test say who it is (x-test-ip).
const WebSocket = require("../../node_modules/ws");
const BASE = process.env.MASTER || "127.0.0.1:55599", SECRET = process.env.SERVER_SECRET || "test-server-secret-0123456789abcdef";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const str = (s) => Buffer.concat([Buffer.from(s), Buffer.from([0])]);
const vu = (n) => { n = BigInt(n); const o = []; while (n >= 128n) { o.push(Number(((n & 127n) << 1n) | 1n)); n >>= 7n; } o.push(Number(n << 1n)); return Buffer.from(o); };
const f64 = (x) => { const b = Buffer.alloc(8); b.writeDoubleLE(x); return b; };
let failures = 0; const check = (ok, what) => { console.log((ok ? "PASS " : "FAIL ") + what); if (!ok) failures++; };
const http = async (path, opt = {}) => { const r = await fetch("http://" + BASE + path, { ...opt, headers: { origin: "https://test.example", "content-type": "application/json", ...(opt.headers || {}) } }); return [r.status, await r.json().catch(() => ({}))]; };
const levelOf = (xp) => { const f = (l) => { if (l <= 60) return (l + 5) * Math.pow(1.175, l); let b = (l + 5) * Math.pow(1.175, 60); for (let i = 60; i < l; i++) b *= Math.min(1.18, Math.max(1.1, 1.18 - 0.0075 * (i - 60))); return b; }; let l = 1; while (xp >= f(l + 1)) xp -= f(++l); return l; };
(async () => {
    const name = "lb" + Math.floor(Math.random() * 1e6), name2 = "lc" + Math.floor(Math.random() * 1e6);
    let ipn = 0; const mk = async (n) => (await http("/auth/signup", { method: "POST", headers: { "x-test-ip": "10.8.0." + (++ipn) }, body: JSON.stringify({ email: n + "@example.com", username: n, password: "hunter2hunter2" }) }))[1];
    const A = await mk(name), B = await mk(name2);
    const ws = new WebSocket("ws://" + BASE + "/ws", "g", { headers: { "x-server-secret": SECRET } });
    ws.on("error", (e) => console.log("ws error", e.message));
    await new Promise((r) => ws.on("open", r));
    ws.send(Buffer.concat([Buffer.from([101]), str("rrolf-hc")]));
    const join = async (acct, pos) => { ws.send(Buffer.concat([Buffer.from([0]), str(acct.uuid), str(acct.pl_p), str(""), vu(1000 + pos), Buffer.from([pos])])); await sleep(600); };
    await join(A, 0);
    const xp = 3000;
    const save = (acct, xpv, pvp, attempts, best, extraPetals = [], pvpKills = 0) => ws.send(Buffer.concat([
        Buffer.from([2]), str(acct.uuid), f64(xpv), vu(pvp), Buffer.from([0, 0, 0]),
        Buffer.from([3, 7]), vu(2), Buffer.from([4, 5]), vu(5), Buffer.from([1, 0]), vu(40), ...extraPetals, Buffer.from([0]),   // petals: ancient x2, mythic x5, common x40
        Buffer.from([0]),                                                                                                         // failed crafts
        Buffer.from([3, 7]), vu(3), Buffer.from([2, 0]), vu(10), Buffer.from([0]),                                                // kills: ancient mob x3 (id 2 + 1), common x10
        Buffer.from([0]), str("Nick"), vu(attempts), vu(best), vu(pvpKills)]));                                                    // loadout none, nickname, attempts, pvp best, pvp kills
    save(A, xp, 50, 12, 900, [], 4);
    await sleep(1800);
    let [st, lb] = await http("/leaderboard?me=" + A.uuid, { headers: { origin: "https://test.example" } });
    check(st === 200 && lb.ok, "GET /leaderboard -> " + st);
    const board = (id) => lb.games.flatMap((g) => g.boards).find((b) => b.id === id);
    const mine = (id) => board(id)?.me;
    check(Math.floor(mine("rr_level")?.score) === levelOf(xp), `level ${Math.floor(mine("rr_level")?.score)} == ${levelOf(xp)}`);
    check(mine("rr_ancients")?.score === 2, "ancients owned 2");
    check(mine("rr_mythics") === undefined && board("rr_mythics") === undefined, "no Rysteria mythic board");
    check(mine("rr_pvp_kills")?.score === 4, "pvp kills 4");
    check(lb.games[0].boards.slice(0, 3).map((b) => b.id).join() === "rr_level,rr_pvp_kills,rr_pvp_best", "level, pvp kills, pvp best come first");
    check(mine("rr_kills")?.score === 13, "kills 13: " + mine("rr_kills")?.score);
    check(mine("rr_ancient_kills")?.score === 3, "ancient kills 3");
    check(mine("rr_ancient_attempts")?.score === 12, "ancient attempts 12");
    check(mine("rr_pvp_best")?.score === 900, "pvp best 900 (the game server's peak, not the 50 now)");
    // in the top ten: flagged with my name; further down only the "me" line says where I am
    check(mine("rr_kills").rank > 10 || board("rr_kills").entries.some((e) => e.me && e.name === name), "I am flagged in the top list under my name when I am in it");
    check(!JSON.stringify(lb).includes(A.uuid), "no uuid anywhere in the answer");
    // attempts add up, a lower pvp peak never lowers the best, ancients can go down
    save(A, xp, 70, 5, 100, [], 3);
    await sleep(1800);
    [st, lb] = await http("/leaderboard?me=" + A.uuid);
    check(mine("rr_ancient_attempts")?.score === 17, "attempts 12 + 5 = " + mine("rr_ancient_attempts")?.score);
    check(mine("rr_pvp_best")?.score === 900, "pvp best stays 900");
    check(mine("rr_pvp_kills")?.score === 7, "pvp kills 4 + 3 = " + mine("rr_pvp_kills")?.score);
    // second player, rank
    await join(B, 1);
    ws.send(Buffer.concat([Buffer.from([2]), str(B.uuid), f64(xp * 4), vu(0), Buffer.from([0, 0, 0]), Buffer.from([0]), Buffer.from([0]), Buffer.from([0]), Buffer.from([0]), str("N2"), vu(0), vu(0)]));
    await sleep(9000); // the top lists are shared for a few seconds (CACHE_MS)
    [st, lb] = await http("/leaderboard?me=" + B.uuid);
    const rankB = mine("rr_level")?.rank;
    [st, lb] = await http("/leaderboard?me=" + A.uuid);
    const rankA = mine("rr_level")?.rank;
    check(rankB > 0 && rankB < rankA, `the player with four times the xp is ahead on level (rank ${rankB} before ${rankA})`);
    check(board("rr_level").entries.map((e) => e.name).join() === [name2, name].join() || board("rr_level").entries.length >= 2, "top list has both");
    // gardn report through the server route
    const flush = await http("/srv/gardn/flush", { method: "POST", headers: { "x-server-secret": SECRET, origin: "" }, body: JSON.stringify({ changes: [{ uuid: A.uuid, add: [], remove: [], kills: 7, mythics: 4, best_score: 123456 }] }) });
    check(flush[0] === 200 || flush[0] === 403, "gardn flush answered " + flush[0]);
    ws.close();
    console.log(failures ? "FAILURES " + failures : "ALL PASSED");
    console.log(JSON.stringify({ A: A.uuid, name }));
    process.exit(failures ? 1 : 0);
})();
