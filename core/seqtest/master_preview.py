# Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Rasterises the draw ops that test_masterfaders dumps (SEQ_MASTER_PREVIEW) into 800 x 480 PNG previews of the Master fader pages (plan 024).
# Developer tool only (needs Pillow); not part of `make test`.   usage: python master_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
PX = {0: 14, 1: 17, 2: 14, 3: 17, 4: 12}   # font handle -> pixel size (F14 F17 F14B F17B F12)
BOLD = {2, 3}


def font(handle):
    names = ("arialbd.ttf", "DejaVuSans-Bold.ttf") if handle in BOLD else ("arial.ttf", "DejaVuSans.ttf")
    for n in names:
        try:
            return ImageFont.truetype(n, PX.get(handle, 14))
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    base = Image.new("RGBA", (W, H), (5, 5, 5, 255))
    d0 = ImageDraw.Draw(base)
    d0.rectangle([0, 0, W, 28], fill=(40, 40, 40, 255))
    d0.text((8, 6), "Global Mixer 3/3 - Master faders (stock title bar)", fill=(160, 160, 160, 255), font=font(0))
    d0.rectangle([0, 425, W, H], fill=(30, 30, 30, 255))
    color, alpha = (255, 255, 255), 255
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line[0] == "#":
            continue
        k, rest = line[0], line[2:]
        if k == "C":
            v = int(rest, 16)
            color = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
        elif k == "A":
            alpha = int(rest)
        elif k == "R":
            x0, y0, x1, y1 = map(int, rest.split())
            ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw(ov).rectangle([x0, y0, x1, y1], fill=color + (alpha,))
            base = Image.alpha_composite(base, ov)
        elif k == "T":
            a = rest.split(" ", 4)
            x, y, f, opt = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            fnt = font(f)
            ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            d = ImageDraw.Draw(ov)
            tw = d.textlength(a[4], font=fnt)
            if opt & 512:
                x -= tw / 2
            if opt & 2048:
                x -= tw
            if opt & 1024:
                y -= PX.get(f, 14) / 2 + 2
            d.text((x, y), a[4], font=fnt, fill=color + (alpha,))
            base = Image.alpha_composite(base, ov)
    base.convert("RGB").save(out)


if __name__ == "__main__":
    for name in sorted(os.listdir(sys.argv[1])):
        if name.startswith("mf_") and name.endswith(".ops"):
            render(os.path.join(sys.argv[1], name), os.path.join(sys.argv[1], name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
