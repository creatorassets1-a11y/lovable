#!/usr/bin/env python3
"""Bake a HUD font into a single-channel atlas plus a binary metrics table.

The engine draws text as textured quads, so it needs glyph rects and advances.
Writing a compact binary avoids shipping a JSON parser in C++ for eleven fields.

Out: assets/tex/font.png  (RGBA, alpha carries coverage)
     assets/tex/font.bin  ('HFNT' | size | line | count | per-glyph metrics)
"""
import os
import struct

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src",
                   "main", "assets", "tex")

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
SIZE = 44
PAD = 3
FIRST, LAST = 32, 126


def build(path, size, out_png, out_bin):
    font = ImageFont.truetype(path, size)
    glyphs = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        box = font.getbbox(ch)
        adv = font.getlength(ch)
        w = max(1, box[2] - box[0])
        h = max(1, box[3] - box[1])
        glyphs.append({"ch": ch, "w": w, "h": h, "bx": box[0], "by": box[1], "adv": adv})

    # Shelf-pack into the smallest power-of-two square that fits.
    for atlas in (256, 512, 1024, 2048):
        x = y = row_h = 0
        ok = True
        for g in glyphs:
            gw, gh = g["w"] + PAD * 2, g["h"] + PAD * 2
            if x + gw > atlas:
                x = 0
                y += row_h
                row_h = 0
            if y + gh > atlas:
                ok = False
                break
            g["x"], g["y"] = x, y
            x += gw
            row_h = max(row_h, gh)
        if ok:
            break

    img = Image.new("L", (atlas, atlas), 0)
    d = ImageDraw.Draw(img)
    for g in glyphs:
        d.text((g["x"] + PAD - g["bx"], g["y"] + PAD - g["by"]), g["ch"], font=font, fill=255)

    # Ship as RGBA with white RGB so the shader can tint it freely.
    rgba = Image.merge("RGBA", (Image.new("L", img.size, 255),) * 3 + (img,))
    rgba.save(out_png, optimize=True)

    ascent, descent = font.getmetrics()
    line = ascent + descent
    with open(out_bin, "wb") as f:
        f.write(b"HFNT")
        f.write(struct.pack("<IIIII", atlas, size, line, FIRST, len(glyphs)))
        for g in glyphs:
            # x, y, w, h in atlas pixels; bearing and advance in pixels.
            f.write(struct.pack("<hhhhhhf",
                                g["x"] + PAD, g["y"] + PAD, g["w"], g["h"],
                                int(g["bx"]), int(g["by"]), float(g["adv"])))

    print(f"  {os.path.basename(out_png)}: {atlas}x{atlas}, {len(glyphs)} glyphs, "
          f"{os.path.getsize(out_png)/1024:.0f} KB")


def main():
    os.makedirs(OUT, exist_ok=True)
    build(FONT_PATH, SIZE, os.path.join(OUT, "font.png"), os.path.join(OUT, "font.bin"))
    build(FONT_BOLD, 56, os.path.join(OUT, "font_title.png"),
          os.path.join(OUT, "font_title.bin"))


if __name__ == "__main__":
    main()
