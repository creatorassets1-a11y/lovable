#!/usr/bin/env python3
"""Generate launcher icons for The Ninth Loop.

The mark is a door standing ajar in the dark with a sliver of sick yellow light
and a silhouette standing in it. It has to survive being shrunk to 48px, so
everything is high contrast and there is no fine detail.
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

RES = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src", "main", "res")

LEGACY = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
# Adaptive icons are 108dp; the inner 72dp is the guaranteed-visible safe zone.
ADAPTIVE = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}

SS = 4  # supersample factor


def draw_door(size, inset):
    """Draw the door-ajar mark on a transparent canvas of `size`.

    `inset` is the fraction of the canvas kept as empty margin, which is what
    separates the tight legacy icon from the padded adaptive foreground.
    """
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    m = S * inset
    box = (m, m, S - m, S - m)
    w = box[2] - box[0]
    h = box[3] - box[1]

    # Doorway: a dark recessed rectangle with slightly rounded top.
    door_w = w * 0.62
    door_x = box[0] + (w - door_w) / 2
    door_y = box[1] + h * 0.06
    door_h = h * 0.94
    d.rounded_rectangle(
        [door_x, door_y, door_x + door_w, door_y + door_h],
        radius=door_w * 0.10,
        fill=(14, 10, 11, 255),
    )
    d.rounded_rectangle(
        [door_x, door_y, door_x + door_w, door_y + door_h],
        radius=door_w * 0.10,
        outline=(96, 30, 26, 255),
        width=int(S * 0.012),
    )

    # The gap: a wedge of light, wider at the bottom as if the door swings inward.
    gap_top = door_x + door_w * 0.40
    gap_bot = door_x + door_w * 0.30
    gap_w_top = door_w * 0.14
    gap_w_bot = door_w * 0.30
    wedge = [
        (gap_top, door_y + door_h * 0.03),
        (gap_top + gap_w_top, door_y + door_h * 0.03),
        (gap_bot + gap_w_bot, door_y + door_h * 0.97),
        (gap_bot, door_y + door_h * 0.97),
    ]

    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).polygon(wedge, fill=(255, 216, 130, 255))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.045))
    img = Image.alpha_composite(img, glow)

    d = ImageDraw.Draw(img)
    d.polygon(wedge, fill=(255, 241, 199, 255))

    # Silhouette standing in the light. Head + shoulders only: at icon size a full
    # body turns to mush.
    cx = (gap_top + gap_bot + gap_w_bot) / 2
    head_r = door_w * 0.105
    head_cy = door_y + door_h * 0.34
    d.ellipse(
        [cx - head_r, head_cy - head_r, cx + head_r, head_cy + head_r],
        fill=(8, 5, 6, 255),
    )
    body_w = head_r * 2.5
    d.polygon(
        [
            (cx - body_w * 0.32, head_cy + head_r * 0.75),
            (cx + body_w * 0.32, head_cy + head_r * 0.75),
            (cx + body_w * 0.62, door_y + door_h * 0.97),
            (cx - body_w * 0.62, door_y + door_h * 0.97),
        ],
        fill=(8, 5, 6, 255),
    )

    return img.resize((size, size), Image.LANCZOS)


def background(size):
    """Deep oxblood vignette."""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 255))
    px = img.load()
    cx = cy = S / 2
    maxd = math.hypot(cx, cy)
    for y in range(S):
        for x in range(0, S, 1):
            dist = math.hypot(x - cx, y - cy) / maxd
            f = max(0.0, 1.0 - dist * 1.15)
            px[x, y] = (
                int(6 + 52 * f * f),
                int(4 + 12 * f * f),
                int(5 + 14 * f * f),
                255,
            )
    return img.resize((size, size), Image.LANCZOS)


def circle_mask(size):
    S = size * SS
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse([0, 0, S - 1, S - 1], fill=255)
    return m.resize((size, size), Image.LANCZOS)


def main():
    for density, size in LEGACY.items():
        out_dir = os.path.join(RES, f"mipmap-{density}")
        os.makedirs(out_dir, exist_ok=True)

        bg = background(size)
        icon = Image.alpha_composite(bg, draw_door(size, 0.10))
        icon.convert("RGB").save(os.path.join(out_dir, "ic_launcher.png"))

        round_icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        round_icon.paste(icon, (0, 0), circle_mask(size))
        round_icon.save(os.path.join(out_dir, "ic_launcher_round.png"))

    for density, size in ADAPTIVE.items():
        out_dir = os.path.join(RES, f"mipmap-{density}")
        os.makedirs(out_dir, exist_ok=True)
        # Foreground gets a fat margin so the launcher's mask cannot clip the mark.
        draw_door(size, 0.28).save(os.path.join(out_dir, "ic_launcher_foreground.png"))
        background(size).convert("RGB").save(
            os.path.join(out_dir, "ic_launcher_background.png")
        )

    print("icons written")


if __name__ == "__main__":
    main()
