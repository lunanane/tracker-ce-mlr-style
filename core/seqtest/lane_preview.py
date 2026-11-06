# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Rasterises the draw ops that the host test dumps (SEQ_LANE_PREVIEW=<dir>) into 800 x 480 PNG previews of the CUT lane screen.
# Developer tool only (needs Pillow); it is not part of `make test`.   usage: python lane_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
BG = (0, 0, 0)
FONT_PX = {1: 14, 2: 17, 3: 14, 4: 17, 5: 12}


def font(n):
    for name in ("arialbd.ttf" if n in (3, 4) else "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, FONT_PX.get(n, 14))
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    img = Image.new("RGBA", (W, H), BG + (255,))
    # the stock title bar and the screen button labels, for orientation
    d0 = ImageDraw.Draw(img)
    d0.rectangle([0, 0, W, 28], fill=(40, 40, 40, 255))
    d0.text((8, 6), "Session view (stock title bar)", fill=(160, 160, 160, 255))
    d0.rectangle([0, 425, W, H], fill=(30, 30, 30, 255))
    labels = ["Assign 1", "Assign 2", "Help", "", "Q beat", "SYNC ALL", "", "CLIPS"]
    for i, t in enumerate(labels):
        d0.text((i * 100 + 50 - 4 * len(t), 445), t, fill=(150, 150, 150, 255))
    color = (255, 255, 255)
    alpha = 255
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        k = line[0]
        a = line[2:].split(" ", 4) if k == "T" else line[2:].split()
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        fill = color + (alpha,)
        if k == "C":
            v = int(a[0], 16)
            color = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
            continue
        if k == "A":
            alpha = int(a[0])
            continue
        if k == "R":
            x0, y0, x1, y1 = map(int, a)
            d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=fill)
        elif k == "D":
            cx, cy, r = map(int, a)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
        elif k == "T":
            x, y, f, opt = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            s = a[4]
            fnt = font(f)
            tw = d.textlength(s, font=fnt)
            th = FONT_PX.get(f, 14)
            if opt & 1:
                x -= tw / 2
            if opt & 4:
                x -= tw
            if opt & 2:
                y -= th / 2
            d.text((x, y), s, font=fnt, fill=fill)
        img = Image.alpha_composite(img, layer)
    img.convert("RGB").save(out)


if __name__ == "__main__":
    folder = sys.argv[1]
    for name in sorted(os.listdir(folder)):
        if name.endswith(".ops"):
            render(os.path.join(folder, name), os.path.join(folder, name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
