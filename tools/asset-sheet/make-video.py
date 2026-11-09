#!/usr/bin/env python3
"""Makes a looping video of a mob, the way it rests next to the way it moves.

    python3 rrolf/tools/asset-sheet/make-video.py "Lanternfly" [--frames 48] [--fps 24]
                                                              [--loops 8] [--out DIR] [--open]

The mob is found by its name in the main list (RR_MOB_NAMES).
Left is the mob at rest, the way the picture it was drawn after shows it (animation timer 0);
right is the same mob going through one round of its animation. The pictures are the real draw
code of the client, run by the same harness the asset sheet uses, and drawn by Chrome (like
the asset sheet), so what the video shows is what the game draws.

Writes DIR/<name>-animation.mp4 (the round `--loops` times) and DIR/<name>-animation.gif (one
round, which a viewer repeats for as long as it is open). Needs a C compiler, Chrome, and
opencv and Pillow for Python.
"""
import argparse
import importlib.util
import json
import math
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def load_gen():
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("gen_asset_sheet", os.path.join(HERE, "gen-asset-sheet.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def find_chrome():
    for p in (os.environ.get("CHROME"), "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
              "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"):
        if p and os.path.exists(p):
            return p
    sys.exit("make-video: Chrome was not found (CHROME=path)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="name of the mob, like Lanternfly")
    ap.add_argument("--frames", type=int, default=48, help="pictures in one round of the animation")
    ap.add_argument("--fps", type=int, default=24)
    ap.add_argument("--loops", type=int, default=8, help="rounds in the mp4 (the gif repeats by itself)")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--open", action="store_true")
    args = ap.parse_args()

    import cv2
    import numpy as np
    from PIL import Image

    gen = load_gen()
    with tempfile.TemporaryDirectory() as tmp:
        exe = gen.build(tmp)

        def run(tick):
            out = subprocess.run([exe, repr(tick)], check=True, capture_output=True, text=True).stdout
            return json.loads(out)

        rest = run(0.0)
        want = args.name.lower()
        found = [i for i, m in enumerate(rest["mobs"]) if m["name"].lower() == want]
        if not found:
            names = ", ".join(m["name"] for m in rest["mobs"])
            sys.exit("make-video: no mob called %r, there are: %s" % (args.name, names))
        kind, idx = "mobs", found[0]
        radius = rest[kind][idx]["radius"]
        static_ops = rest[kind][idx]["composite"]
        frames = []
        for f in range(args.frames):
            frames.append(run(2 * math.pi * f / args.frames)[kind][idx]["composite"])

    with open(os.path.join(HERE, "page.html"), encoding="utf-8") as fh:
        page = fh.read()
    replay = re.search(r"function replay\(ctx, ops, base\) \{.*?\n\}\n", page, re.S).group(0)

    pw, ph, cols = 360, 420, 4
    rows = (args.frames + cols - 1) // cols
    k = ph * 0.42 / (radius * 1.45)  # pixels to a unit: the mob fills the panel
    html = """<!doctype html><meta charset=utf-8><body style="margin:0;background:#e9eaee">
<div id=g style="width:%dpx;display:flex;flex-wrap:wrap"></div><script>%s
const stat=%s, frames=%s, PW=%d, PH=%d, K=%f;
const g=document.getElementById('g');
frames.forEach((ops,i)=>{
  const c=document.createElement('canvas'); c.width=2*PW; c.height=PH; c.style.display='block';
  g.appendChild(c); const ctx=c.getContext('2d');
  ctx.fillStyle='#e9eaee'; ctx.fillRect(0,0,2*PW,PH);
  // the head is up, like the pictures of the bugs: the game's +x is the page's up
  replay(ctx, stat, [0,-K,K,0,PW/2,PH/2]);
  replay(ctx, ops, [0,-K,K,0,PW+PW/2,PH/2]);
  ctx.setTransform(1,0,0,1,0,0); ctx.globalAlpha=1; ctx.fillStyle='#555'; ctx.font='16px sans-serif';
  ctx.textAlign='center'; ctx.fillText('rest',PW/2,26); ctx.fillText('animated',PW+PW/2,26);
  ctx.fillStyle='#bbb'; ctx.fillRect(PW-1,0,2,PH);
});
</script>""" % (cols * 2 * pw, replay, json.dumps(static_ops), json.dumps(frames), pw, ph, k)

    os.makedirs(args.out, exist_ok=True)
    slug = re.sub(r"[^a-z0-9]+", "-", args.name.lower()).strip("-")
    with tempfile.TemporaryDirectory() as tmp:
        page_path = os.path.join(tmp, "frames.html")
        shot = os.path.join(tmp, "frames.png")
        with open(page_path, "w", encoding="utf-8") as fh:
            fh.write(html)
        subprocess.run([find_chrome(), "--headless=new", "--disable-gpu",
                        "--window-size=%d,%d" % (cols * 2 * pw, rows * ph), "--virtual-time-budget=20000",
                        "--screenshot=" + shot, "file://" + page_path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sheet = cv2.imread(shot)
    if sheet is None or sheet.shape[1] < cols * 2 * pw:
        sys.exit("make-video: Chrome did not draw the pictures")
    pics = []
    for i in range(args.frames):
        x, y = (i % cols) * 2 * pw, (i // cols) * ph
        pics.append(sheet[y:y + ph, x:x + 2 * pw].copy())

    mp4 = os.path.join(args.out, slug + "-animation.mp4")
    writer = None
    for code in ("avc1", "mp4v"):
        writer = cv2.VideoWriter(mp4, cv2.VideoWriter_fourcc(*code), args.fps, (2 * pw, ph))
        if writer.isOpened():
            break
    for _ in range(args.loops):
        for p in pics:
            writer.write(p)
    writer.release()
    gif = os.path.join(args.out, slug + "-animation.gif")
    imgs = [Image.fromarray(cv2.cvtColor(p, cv2.COLOR_BGR2RGB)) for p in pics]
    imgs[0].save(gif, save_all=True, append_images=imgs[1:], duration=int(1000 / args.fps), loop=0)
    print("wrote", mp4)
    print("wrote", gif)
    if args.open:
        subprocess.run(["open", mp4], check=False)


if __name__ == "__main__":
    main()
