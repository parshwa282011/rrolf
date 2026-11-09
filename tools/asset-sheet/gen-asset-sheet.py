#!/usr/bin/env python3
"""Draws every mob and petal of the client on one html page, to look for discrepancies.

    python3 rrolf/tools/asset-sheet/gen-asset-sheet.py [--out DIR] [--tick T] [--open]

It does not parse the drawing code. harness.c compiles the real draw functions of
Client/Assets (RenderMobs.c, RenderPetals.c, Mob/*.c, ...) against a renderer that
writes every instruction down, so loops, helpers and maths run as in the game. The
page replays those instructions on a canvas. Needs only a C compiler (clang or gcc).

The page shows, for every mob, the composite exactly as rr_renderer_draw_mob puts it
together next to each sprite of its sheet alone, and marks

  * sprites that are in the sheet but that no composite draws (and how often the
    others are drawn),
  * sprites that reach outside their cell (they are cut out of a shared cache),
  * drawings far bigger or smaller than the mob's hitbox radius,
  * mobs or petals that draw nothing, petals that look the same at every rarity,

and for every petal draw_petal and draw_static_petal at every rarity. This script also
lists the draw functions of Client/Assets that nothing references (read from the source).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))  # rrolf/


def find_compiler():
    for c in (os.environ.get("CC"), "clang", "gcc", "cc"):
        if c and shutil.which(c):
            return c
    sys.exit("asset-sheet: no C compiler (clang or gcc) found; set CC")


def build(tmp):
    cc = find_compiler()
    base = [cc, "-w", "-O1", "-I" + ROOT]
    assets = []
    for d, _, files in os.walk(os.path.join(ROOT, "Client", "Assets")):
        for f in sorted(files):
            if f.endswith(".c") and f != "Init.c":  # Init.c starts the real caches
                assets.append(os.path.join(d, f))
    common = os.path.join(tmp, "common.o")
    # Common.c is the real renderer helpers (matrices, filters); its spritesheet code is
    # swapped for the one of the harness, which records the sprites instead of drawing
    subprocess.run(base + ["-c", "-Drr_renderer_spritesheet_init=real_ss_init",
                           "-Drender_sprite_from_cache=real_rsfc", "-o", common,
                           os.path.join(ROOT, "Client", "Renderer", "Common.c")], check=True)
    exe = os.path.join(tmp, "harness")
    subprocess.run(base + ["-o", exe, os.path.join(HERE, "harness.c"), common] + sorted(assets) + [
        os.path.join(ROOT, "Shared", "StaticData.c"),
        os.path.join(ROOT, "Shared", "Utilities.c"),
        os.path.join(ROOT, "Shared", "Vector.c"), "-lm"], check=True)
    return exe


def unreferenced_draw_functions():
    """Non-static draw functions of Client/Assets that no other line of the client mentions."""
    decl_files = {os.path.join(ROOT, "Client", "Assets", n) for n in ("Render.h", "RenderFunctions.h")}
    defs = {}
    text = {}
    for d, _, files in os.walk(os.path.join(ROOT, "Client")):
        for f in files:
            if f.endswith((".c", ".h", ".cc")):
                p = os.path.join(d, f)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    text[p] = fh.read()
    define = re.compile(r"^void (rr_\w+|asset_\w+)\(struct rr_renderer \*\w*[^)]*\)\s*$", re.M)
    for p, t in text.items():
        if os.path.join("Client", "Assets") + os.sep not in p or not p.endswith(".c"):
            continue
        for m in define.finditer(t):
            defs[m.group(1)] = (os.path.relpath(p, ROOT), t.count("\n", 0, m.start()) + 1)
    out = []
    for name, (file, line) in sorted(defs.items()):
        word = re.compile(r"\b" + re.escape(name) + r"\b")
        refs = 0
        for p, t in text.items():
            if p in decl_files:
                continue
            refs += len(word.findall(t))
        refs -= 1  # its own definition
        if refs <= 0:
            out.append({"name": name, "file": file, "line": line})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(HERE, "out"), help="folder for asset-sheet.html (default tools/asset-sheet/out)")
    ap.add_argument("--tick", type=float, default=0.0, help="animation timer the mobs are drawn at (radians, default 0)")
    ap.add_argument("--open", action="store_true", help="open the page in the browser")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp)
        raw = subprocess.run([exe, str(args.tick)], check=True, capture_output=True, text=True).stdout
    data = json.loads(raw)
    data["unreferenced_functions"] = unreferenced_draw_functions()

    with open(os.path.join(HERE, "page.html"), encoding="utf-8") as fh:
        page = fh.read()
    blob = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    page = page.replace("/*DATA*/", blob, 1)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "asset-sheet.html")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(page)

    # the part of the report that needs no browser
    print(f"{len(data['mobs'])} mobs, {len(data['petals'])} petals -> {path}")
    for m in data["mobs"]:
        for p in m["parts"]:
            if p["used"] == 0 and p["used_alt"] == 0:
                print(f"  unused sprite: {m['name']} slot {p['slot']} ({p['w']:g}x{p['h']:g})")
        if not m["composite"]:
            print(f"  draws nothing: mob {m['name']}")
    for p in data["petals"]:
        if not p["raw"] and p["id"] != 0:  # petal 0 is the empty placeholder
            print(f"  draws nothing: petal {p['name']}")
    for f in data["unreferenced_functions"]:
        print(f"  unreferenced draw function: {f['name']}  ({f['file']}:{f['line']})")
    if args.open:
        webbrowser.open("file://" + path)


if __name__ == "__main__":
    main()
