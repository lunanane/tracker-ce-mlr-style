# Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Draws the Global Mixer 3/3 page with the Rec button (plan 030 A) from the texts that test_masterrec dumps (SEQ_REC_PREVIEW=<dir>).
# A schematic of the stock page (title bar, four level bars, the label strip), not a render of the FT81x frame. Developer tool, needs Pillow.
# usage: python rec_preview.py <dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
GREY_32, GREY_77, WHITE, RED = (0x32, 0x31, 0x32), (0x77, 0x77, 0x77), (255, 255, 255), (0xCD, 0x18, 0x18)


def font(px, bold=False):
    for n in (("arialbd.ttf", "DejaVuSans-Bold.ttf") if bold else ("arial.ttf", "DejaVuSans.ttf")):
        try:
            return ImageFont.truetype(n, px)
        except OSError:
            continue
    return ImageFont.load_default()


def render(path, out):
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 28], fill=(8, 8, 8))
    d.line([0, 28, W, 28], fill=RED)
    labels = {}
    for line in open(path, encoding="utf-8"):
        k, _, rest = line.rstrip("\n").partition(" ")
        if k == "title":
            d.text((9, 6), rest, fill=WHITE, font=font(16))
        elif k == "right":
            tw = d.textlength(rest, font=font(16))
            d.text((790 - tw, 6), rest, fill=WHITE, font=font(16))
        elif k == "label":
            i, col, text = rest.split(" ", 2)
            labels[int(i)] = (col, text)
    names = ["Delay", "Reverb", "Dry Mix", "Line In"]
    for i, n in enumerate(names):
        x = 100 * i + 50
        tw = d.textlength(n, font=font(14))
        d.rectangle([100 * i + 3, 32, 100 * i + 97, 58], fill=(8, 8, 8))
        d.text((x - tw / 2, 38), n, fill=WHITE, font=font(14))
        for b, hgt in ((0, 150 + 30 * i), (1, 130 + 20 * i)):
            bx = 100 * i + 22 + b * 34
            d.rectangle([bx, 60, bx + 24, 400], fill=(10, 10, 10))
            d.rectangle([bx, 400 - hgt, bx + 24, 400], fill=(0, 190, 70) if hgt < 280 else (230, 160, 30))
    d.rectangle([0, 425, W, H], fill=(10, 10, 10))
    for i in range(8):
        col, text = labels.get(i, ("grey", ""))
        bg = RED if col == "red" else GREY_32
        fg = WHITE  # theme: performActiveTrackLabelsColors = red fill, white text (red) / activeLabelsColors text2 = white (grey)
        d.rectangle([100 * i + 3, 424, 100 * i + 97, 478], fill=bg)
        tw = d.textlength(text, font=font(17))
        d.text((100 * i + 50 - tw / 2, 442), text, fill=fg, font=font(17))
    img.save(out)


if __name__ == "__main__":
    for name in sorted(os.listdir(sys.argv[1])):
        if name.startswith("rec_") and name.endswith(".ops"):
            render(os.path.join(sys.argv[1], name), os.path.join(sys.argv[1], name[:-4] + ".png"))
            print("wrote", name[:-4] + ".png")
