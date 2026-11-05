# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Rasterises the draw ops that test_amp dumps (SEQ_AMP_PREVIEW=<dir>) into 800 x 480 PNG previews of the amplitude view.
# Developer tool only (needs Pillow); not part of `make test`.   usage: python amp_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
FONT_PX = {1: 13, 2: 17, 3: 17}


def font(n):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, FONT_PX.get(n, 14))
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    pitch = os.path.basename(path).startswith("pitch")
    d.rectangle([0, 0, W, 28], fill=(40, 40, 40))
    d.text((8, 6), "PolySynth Instrument (stock title bar)" if pitch else "Master, Global Mixer 3/3 (stock title bar)", fill=(160, 160, 160))
    d.rectangle([0, 425, W, H], fill=(30, 30, 30))
    for i, t in enumerate(["", "", "", "", "", "", "Pitch view" if pitch else "", "" if pitch else "Close"]):
        d.text((i * 100 + 50 - 4 * len(t), 445), t, fill=(150, 150, 150))
    color = (255, 255, 255)
    wd = 1
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        k, rest = line[0], line[2:]
        if k == "C":
            v = int(rest, 16)
            color = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
        elif k == "W":
            wd = max(1, round(int(rest) / 16.0))
        elif k == "R":
            x0, y0, x1, y1 = map(int, rest.split())
            d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=color)
        elif k == "L":
            v = list(map(int, rest.split()))
            for i in range(0, len(v), 4):
                d.line([v[i], v[i + 1], v[i + 2], v[i + 3]], fill=color, width=1)
        elif k == "S":
            v = list(map(int, rest.split()))
            pts = [(v[i], v[i + 1]) for i in range(0, len(v), 2)]
            d.line(pts, fill=color, width=max(1, wd), joint="curve")
        elif k == "T":
            a = rest.split(" ", 4)
            x, y, f, opt = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            fnt = font(f)
            tw = d.textlength(a[4], font=fnt)
            if opt & 1:
                x -= tw / 2
            if opt & 4:
                x -= tw
            if opt & 2:
                y -= FONT_PX.get(f, 14) / 2 + 2
            d.text((x, y), a[4], font=fnt, fill=color)
    img.save(out)


if __name__ == "__main__":
    for name in sorted(os.listdir(sys.argv[1])):
        if (name.startswith("amp") or name.startswith("pitch")) and name.endswith(".ops"):
            render(os.path.join(sys.argv[1], name), os.path.join(sys.argv[1], name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
