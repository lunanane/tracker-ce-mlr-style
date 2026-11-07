# Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Rasterises the draw ops that preview_legend dumps into 800 x 480 PNG previews of the Play legend with the sequencer settings page (plan 025).
# Developer tool only (needs Pillow); not part of `make test`.   usage: python legend_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
SS = 2


def font(px, bold):
    for name in (("arialbd.ttf", "DejaVuSans-Bold.ttf") if bold else ("arial.ttf", "DejaVuSans.ttf")):
        try:
            return ImageFont.truetype(name, px * SS)
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    img = Image.new("RGB", (W * SS, H * SS), (5, 5, 5))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W * SS, 28 * SS], fill=(40, 40, 40))
    d.text((8 * SS, 6 * SS), "Play mode (stock title bar)", fill=(160, 160, 160), font=font(13, False))
    d.rectangle([0, 425 * SS, W * SS, H * SS], fill=(30, 30, 30))
    for i in range(8):
        d.text(((i * 100 + 20) * SS, 440 * SS), "T%d" % (i + 1), fill=(150, 150, 150), font=font(13, False))
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
            d.rectangle([x0 * SS, y0 * SS, (x1 + 1) * SS - 1, (y1 + 1) * SS - 1], fill=color)
        elif k == "S":
            v = list(map(int, rest.split()))
            d.line([(v[i] * SS, v[i + 1] * SS) for i in range(0, len(v), 2)], fill=color, width=wd * SS)
        elif k == "T":
            a = rest.split(" ", 4)
            x, y, f, opt = int(a[0]) * SS, int(a[1]) * SS, int(a[2]), int(a[3])
            fnt = font(14, f == 2)
            tw = d.textlength(a[4], font=fnt)
            if opt & 512:
                x -= tw / 2
            if opt & 2048:
                x -= tw
            if opt & 1024:
                y -= (7 + 2) * SS
            d.text((x, y), a[4], font=fnt, fill=color)
    img.resize((W, H), Image.LANCZOS).save(out)


if __name__ == "__main__":
    for name in sorted(os.listdir(sys.argv[1])):
        if name.startswith("legend_") and name.endswith(".ops"):
            render(os.path.join(sys.argv[1], name), os.path.join(sys.argv[1], name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
