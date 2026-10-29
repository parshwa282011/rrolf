# CrazyGames requirements for Rysteria

Written 2026-10-24 from the current CrazyGames docs, for the `gardn-update` branch. This file lives in `scripts/`, which is gitignored, so it is not in any commit.

Sources: [requirements](https://docs.crazygames.com/requirements/intro/) and its pages (technical, gameplay, advertisement, account integration, multiplayer, game covers, quality), [Basic Launch guide](https://docs.crazygames.com/resources/basic-launch-metrics/), [SDK](https://docs.crazygames.com/sdk/intro/).

Marks: `[x]` the code or setup is there (looked at in the tree, not re-tested on CrazyGames), `[ ]` open or not checked, `[!]` a risk to decide about.

## 0. How the launch works

- **Basic Launch** first: ads are off, no revenue. It lasts at least 7 days and until 500 plays, whichever is later; if there are fewer than 500 plays after 21 days it ends anyway.
- Measured on the developer dashboard (updated daily). The guide gives benchmarks, not fixed cut-offs:
  - average play time (good games reach 10+ minutes)
  - day 1 retention (strong games 10-15%)
  - conversion = players who play at least one minute after starting (top games 80%+, load under 10 s, build under 20 MB)
- **Full Launch** after that for games with strong numbers: SDK events, ads, account integration and the multiplayer features below.
- Rysteria is an iframe of `https://game.parshwa.blog` (servers on the Mac `c1` behind the `rysteria-cg` cloudflared tunnel).

## 1. Technical

### Size and files (Basic)
- Initial download 50 MB or less (20 MB or less to be eligible for the mobile homepage).
- Total file size 250 MB or less (50 MB without the SDK). File count 1500 or less.
- Measured from the start of loading to the first `gameplayStart`; without the SDK the total file size counts.
- Files that are hosted elsewhere (this game is) are judged by time to gameplay: **20 seconds or less**.
- Relative paths only for files in the bundle.
- [x] The client is `Client/public`: 7 files, about 864 KB (wasm 735 KB). Far below every limit.
- [ ] Time from opening the page to being in the game, measured through the tunnel on the real iframe (the wasm is small, the wait is the servers answering).
- [x] Ubuntu font is shipped in `Client/public/fonts/` instead of Google Fonts.

### Devices and browsers (Basic)
- Must work on Chrome and Edge. Games that do not work well on Safari are disabled there. Disabled on Chromium OS if they do not run well on 4 GB of RAM.
- Mouse, keyboard and touch if mobile is supported. Landscape for desktop games.
- Mobile: runs fullscreen in the CrazyGames App and must respect safe-area padding. Add `user-select: none` (with the `-webkit-`, `-moz-`, `-ms-` forms) to `body`.
- iOS audio: after the app is foregrounded, `resume()` a suspended AudioContext inside a user gesture (`touchend` or `click`).
- [ ] Chrome and Edge checked, Safari and Firefox checked.
- [ ] Mobile touch controls work in the CrazyGames App, with safe areas.
- [ ] `user-select: none` on `body` in `Client/public/index.html`.
- [ ] Arrow keys, space and the mouse wheel do not scroll the page; context menu blocked.

### SDK (Basic and Full)
- Basic: send `gameplayStart` when the player reaches gameplay. Ads are disabled and ignored in Basic.
- Full: `gameplayStart` / `gameplayStop`; use the Data module for progress saving and the User module for accounts; optional `loadingStart` / `loadingStop`.
- [x] SDK loaded and `await SDK.init()` run before the wasm starts (`Client/public/platform.js`), only when framed or with `?useLocalSdk=true`.
- [x] Three modes: `crazygames`, `local`, plain web. The game works with the SDK environment `disabled`.
- [x] `loadingStart` / `loadingStop`, `gameplayStart` while alive in the arena, `gameplayStop` on death, menu or lost connection (`Client/Platform.c`).

### Sitelock and consent
- Sitelock is optional; if used, whitelist every `crazygames.com` origin and the iOS and Android app origins.
- If the game collects personal data beyond SDK events, show Terms and/or a Privacy notice to new players (a small notice, not a blocking pop-up).
- [x] "Terms & Privacy" panel (`Client/Ui/TitleScreen/Legal.c`). Check that its text is still true.
- [ ] Sitelock header confirmed working (or left off on purpose).

## 2. Gameplay (Basic)

- Text and images legible at devicePixelRatio 1, and at 16:9 responsive iframe sizes (907x510, 1077x606, 1216x684, 1366x768, 1920x1080) and mobile (800x450).
- Physics the same at 60, 144 and 165 Hz.
- English text (translations accurate if any). Intuitive controls on every device. No errors or crashes.
- Original name, assets and content.
- No custom fullscreen button. No cross-promotion of other games or platforms (community links and same-series links are fine if they are not the main menu call to action; App Store links never).
- PEGI 12: suitable for players 13 and older.
- Full: new players land in gameplay at once, at most one click.
- [x] Custom fullscreen button hidden on CrazyGames. Gardn button (another game) hidden. "Are you sure" unload prompt removed.
- [x] GitHub and Discord buttons are switched off on the title screen for now (`Client/Game.c`); the Discord community link was meant to be allowed.
- [x] "Hide UI" setting and hotkey disabled on CrazyGames.
- [ ] Escape: it exits fullscreen in the browser and also leaves the squad in the game. Quality guide says avoid Escape.
- [ ] Physics checked at 60/144/165 Hz. The server runs at 25 ticks per second and the client interpolates.
- [ ] Readable at every size above and at DPR 1.
- [ ] One click from the title screen to gameplay (Play is the one click; the Garden / Hell Creek / PvP / CTF buttons are a choice before it).
- [ ] Typos checked.
- [!] Originality: a florr-style game; CrazyGames asks for original names and content.

## 3. Advertisement

- Basic: only ads through the CrazyGames SDK; ads are off. Nothing may break with ads off (no rewarded-ad buttons that do nothing).
- Full: midgame ads at logical points (not on menu, settings or shop buttons, not mid-fight), game paused and muted only while an ad plays, handle `adError`. Rewarded ads are optional extras (never required, not during active gameplay, no chaining, skip button as visible as the request button, a way to get the reward without the ad, no reward on `adError`). Must work with AdBlock without popups. Banners only on screens open 5 s or more, at most 2 per screen, never over the UI.
- [x] No ads in the game at all, so Basic is met. Full needs ads added through the SDK.

## 4. Account integration (only if applicable)

- Basic: players can play as guests by default (CrazyGames users too); no external login options (Facebook, Google, email, ...).
- Full: show the CrazyGames username and avatar; if `getUser` is null continue as guest; new logged-in CrazyGames users are registered and logged in automatically, returning ones too; guests can still play; logging out in the game and offering external logins is not allowed. Progress must be saved with one of: the Data module, or Automatic Progress Save (not allowed with in-game purchases).
- [x] Guests can always play.
- [x] "Link CrazyGames" in the account panel and a CrazyGames login button; tokens verified in `MasterServer/crazygames.js` (`CG_ALLOW_DEMO=1` for demo tokens).
- [x] CrazyGames username fills the nickname and shows top right.
- [ ] Avatar is not shown.
- [ ] Real `gameId` read from a real token and `CG_GAME_ID` set in `MasterServer/.env`.
- [!] Discord login / linking is still in the account panel; CrazyGames bans external login options. The Discord title button is off, the account panel option is not.
- [!] Progress is saved on your own account server, with the Data module not used by decision. Full Launch asks for the Data module or Automatic Progress Save.

## 5. Multiplayer (only if applicable)

- Chat: follow the game setting `disableChat`; chat needs moderation (a profanity filter is the minimum; Lasso AI moderation is not recommended for Basic). If there are complaints, chat must be turned off.
- Full (online with friends): `updateRoom` with a room id unique within the game and `isJoinable`, `inviteParams`; optional `inviteLink`; Instant Multiplayer (`isInstantMultiplayer` puts the first player straight into a new private room; 20+ player games may go to public play); round-based games keep the group between rounds; submit the lobby size; show CrazyGames usernames in game.
- [x] Client profanity filter on chat and nicknames (`Client/Filter.c`); chat hidden and not sent when `disableChat` is on.
- [x] `updateRoom` / `leftRoom` with the squad code, invite parameter `code`, join-room listener, instant multiplayer, "Copy Link" in the squad panel.
- [x] PvP leaderboard reporting to CrazyGames (`MasterServer/crazygames.js`).
- [ ] Server-side profanity filter.
- [ ] Leaderboard enabled by CrazyGames (invite only) and `CG_LEADERBOARD_API_KEY` set.
- [ ] Lobby size entered at submission.
- [ ] Room codes: they start with the server prefix (`hc-`, `pvp-`, `ctf-` before). Garden is `ga-` and works the same.
- [ ] A new server needs its own hostname through the tunnel. The client connects to `wss://<name>-game.parshwa.blog`: `default`, `pvp`, `ctf` and now `garden`. `garden-game.parshwa.blog` has to exist in the tunnel before the Garden works from CrazyGames.

## 5b. In-game purchases
- Invite only; not available in Basic. Not used.

## 6. Game covers and metadata (submission)

- Description and controls text.
- Covers, all three, same visuals: landscape 1920x1080 (16:9), portrait 800x1200 (2:3), square 800x800 (1:1). No borders; only the game title as text (no "New", "Updated", "Play"); no icons or store logos; no copyrighted visuals; no blur or pixelation.
- Preview videos (recommended, shown on hover): landscape 1080p 16:9 and portrait 1080p 2:3, 15-20 seconds, 50 MB or less, silent, no default mouse cursor, no black bars, black screens or logo transitions, no "Play Now" text or app icons, no fast-forwarding, first frame = the static cover.
- [ ] Game created in the Developer Portal ("Submit a game"), iframe URL `https://game.parshwa.blog`.
- [ ] Description and controls written.
- [ ] Three covers made. Two preview videos made.
- [ ] Game tested in `crazygames.com/preview` with the real SDK.

## 7. Quality guidelines (recommended, not required)

- Onboarding that is part of the game, skippable, mostly visual. Show the controls (keyboard overlay or mouse gestures), adapt to the user's keyboard layout, avoid Escape and Ctrl/Cmd+W.
- Clear button labels, no delays that nudge players; layout for the device.
- Fast response, smooth play. Clear reachable goals; solo players enjoy it as much as groups or it is said that solo is not available.
- Consistent audio levels and art style. Easy to expand; keep the genre after submission; keep updating. Distinctive name.
- [x] A tutorial exists (`Client/Ui/InGame/Tutorial.c`). [ ] Check it is skippable.

## 8. Things in this repo that matter for a CrazyGames review

- [!] The servers are built with `-DPDEV=1`: anyone who knows the dev PIN can open the "+" panel in the client, give themselves any petal and use dev cheats. Fine for testing on `c1`, but for the version CrazyGames reviews the PIN backdoor should be gone (build without PDEV; note the non-PDEV wasm client did not build when last tried, see the memory note).
- [ ] The accounts database was wiped on 2026-10-24; old copies are in `~/rrolf-database-backup` on `c1`.
- [ ] `SERVER_SECRET` out of the source and into the environment (`MasterServer/main.js`, `Shared/Api.h`); old Rivet token, ngrok token and Discord webhook rotated or removed.
- [ ] The master server stays off the tunnel (it listens on loopback only).
- [ ] `c1` stays on and connected for the whole review. It also runs a Minecraft server that uses 6 GB of memory.
- [!] AGPL: players must be offered the source; decide where the link goes (the GitHub button is off for now).

## 9. Handy commands

```
scripts/build.sh                          build the client and all servers
scripts/dev-up.sh [--ctf-enable]          run everything on this Mac
scripts/remote-deploy.sh -y [--ctf-enable]  copy to c1 and start, detached
scripts/remote-stop.sh                    stop everything on c1
scripts/tunnel-setup.sh --config-only     rewrite the tunnel config (adds garden-game)
ssh tejashshah@c1.local 'tail -f rrolf-run/logs/*.log'
```
