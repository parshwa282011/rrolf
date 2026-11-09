# Asset sheet

One html page with every mob and petal the client draws, to look for discrepancies.

    python3 rrolf/tools/asset-sheet/gen-asset-sheet.py --open

Writes `tools/asset-sheet/out/asset-sheet.html` (git-ignored). Needs only Python 3 and
a C compiler (`clang`/`gcc`, or `CC=...`), no emscripten and no built client.

## How it reads the draw code

It does not parse C. `harness.c` compiles the real `Client/Assets/**.c` (mobs, petals,
tiles, props) together with `Client/Renderer/Common.c` against a renderer that writes
each instruction down as JSON instead of drawing (same instruction numbers as
`Client/Renderer/Wasm.c`). Loops, helper functions and maths therefore run as they do
in the game. `page.html` replays the instructions on a canvas.

`Init.c` is left out (it starts the real caches) and the sprite sheet code of `Common.c`
is swapped for the harness's, which records every sprite of every sheet and counts how
often the composite of a mob (`rr_renderer_draw_mob`) draws it.

## What it marks

| mark | meaning |
|---|---|
| sprite **never drawn** | in a mob's sheet but no composite draws it (rasterized for nothing, or forgotten) |
| **out of its cell** | the sprite reaches outside its `w×h` cell; the game cuts the cell out of a shared cache |
| **reach ×N** | the drawing reaches N hitbox radii (flagged under 0.4 or over 3) |
| **draws nothing** | a mob or petal that produces no instructions |
| unreferenced functions | non-static draw functions of `Client/Assets` that no other line of the client mentions (read from the source) |

Mobs are drawn with the flags of everything but a centipede head; mobs that use other
sprites with the head flags (the house centipede) get a second drawing. Petals are shown
raw (`rr_renderer_draw_petal`) and at every rarity (`rr_renderer_draw_static_petal`, the
loadout look). `--tick T` draws the mobs at another animation time (radians).

The page has switches for the hitbox circle, the sprite cells, a light page, zoom,
"only flagged" and a name search.
