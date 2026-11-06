# Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Rasterises the draw ops that preview_grid dumps into 800 x 480 PNG previews of the clip launcher (plan 017). Developer tool only
# (needs Pillow); not part of `make test`.   usage: python grid_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480


def font(px):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    img = Image.new("RGB", (W, H), (5, 5, 5))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 28], fill=(40, 40, 40))
    d.text((8, 6), "Clip launcher (stock title bar)", fill=(160, 160, 160), font=font(14))
    d.rectangle([0, 425, W, H], fill=(30, 30, 30))
    color, wd = (255, 255, 255), 1
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line[0] == "#":
            continue
        k, rest = line[0], line[2:]
        if k == "C":
            v = int(rest, 16)
            color = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
        elif k == "W":
            wd = max(1, round(int(rest) / 16.0))
        elif k == "R":
            x0, y0, x1, y1 = map(int, rest.split())
            d.rectangle([x0, y0, x1, y1], fill=color)  # RECTS include both corners
        elif k == "S":
            v = list(map(int, rest.split()))
            d.line([(v[i], v[i + 1]) for i in range(0, len(v), 2)], fill=color, width=wd)
        elif k == "T":
            a = rest.split(" ", 4)
            x, y, f, opt = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            px = 12 if f == 4 else 14
            fnt = font(px)
            tw = d.textlength(a[4], font=fnt)
            if opt & 512:
                x -= tw / 2
            if opt & 1024:
                y -= px / 2 + 2
            d.text((x, y), a[4], font=fnt, fill=color)
    img.save(out)


if __name__ == "__main__":
    for name in sorted(os.listdir(sys.argv[1])):
        if name.startswith("grid_") and name.endswith(".ops"):
            render(os.path.join(sys.argv[1], name), os.path.join(sys.argv[1], name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
