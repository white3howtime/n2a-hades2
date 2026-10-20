#!/usr/bin/env python
"""Build the launcher icon from YOUR OWN title-screen screenshot (game art is never stored in the repo).
usage: hades2_icon.py <title screenshot.png> <out res dir>   (crop box is for a 2640x1216 phone screenshot; edit BOX for others)
then rebuild: scripts/rebuild.sh picks up .local/icon-res automatically."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
BOX = (290, 115, 1250, 600)   # the "HADES II" logo on the title screen
im = Image.open(sys.argv[1]).convert("RGB")
if im.size != (2640, 1216):
    sx = im.width / 2640; BOX = tuple(int(v * sx) for v in BOX)
crop = im.crop(BOX); S = 512
sc = S / crop.height; bgc = crop.resize((int(crop.width * sc), S)); l = (bgc.width - S) // 2
bg = Image.blend(bgc.crop((l, 0, l + S, S)).filter(ImageFilter.GaussianBlur(26)), Image.new("RGB", (S, S), (4, 14, 10)), .55)
fh = int(crop.height * S / crop.width); fg = crop.resize((S, fh), Image.LANCZOS)
m = Image.new("L", (S, fh), 0); ImageDraw.Draw(m).rectangle((26, 26, S - 26, fh - 26), fill=255)
bg.paste(fg, (0, (S - fh) // 2), m.filter(ImageFilter.GaussianBlur(14)))
for d, sz in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
    o = Path(sys.argv[2]) / f"mipmap-{d}"; o.mkdir(parents=True, exist_ok=True)
    bg.resize((sz, sz), Image.LANCZOS).save(o / "ic_launcher.png")
