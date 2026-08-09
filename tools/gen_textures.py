#!/usr/bin/env python3
"""Author every material in the game.

Each entry builds a height field first, then derives albedo, normal and ORM from
it, so grime, cavity occlusion and surface relief always agree with each other.

Run: python3 tools/gen_textures.py [name ...]
Out: android/app/src/main/assets/tex/<name>_{a,n,orm}.png
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texlib import (  # noqa: E402
    brick_grid, fbm, height_to_ao, height_to_normal, norm01, save_gray_packed,
    save_rgb, smoothstep, streaks, tint, worley,
)

OUT = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src",
                   "main", "assets", "tex")

HERO = 1024
STD = 512


# ------------------------------------------------------------------ walls

def tex_wall_tile(S=HERO):
    """Hospital ceramic tile: pale green, crazed glaze, filthy grout."""
    mask, u, v, cell = brick_grid(S, 8, 16, mortar=0.045, stagger=0.0)
    grime = fbm(S, 4, 6, seed=1)
    fine = fbm(S, 32, 4, seed=2)
    crackle = 1.0 - worley(S, 26, seed=3, kind="f2f1")
    run = streaks(S, 70, seed=4, length=0.7)

    h = mask * 0.5 + 0.5
    h -= (1 - mask) * 0.35
    h += fine * 0.03
    h -= np.clip(crackle - 0.72, 0, 1) * 0.6      # hairline cracks in the glaze
    h -= run * 0.02

    # Per-tile colour variation, plus a few tiles missing entirely.
    variation = (cell % 7) / 7.0
    missing = (cell % 23 < 1).astype(np.float32)
    h -= missing * 0.55

    base = tint(np.clip(0.62 + variation * 0.22 - grime * 0.28, 0, 1),
                (0.52, 0.57, 0.52), (0.86, 0.90, 0.84))
    grout = tint(np.clip(0.30 - grime * 0.25, 0, 1),
                 (0.13, 0.12, 0.10), (0.36, 0.34, 0.29))
    alb = base * mask[..., None] + grout * (1 - mask)[..., None]
    alb *= (1 - run * 0.45)[..., None]
    alb = alb * (1 - missing * 0.6)[..., None] + \
        np.array([0.22, 0.17, 0.15]) * (missing * 0.6)[..., None]
    # Soot rising from the fire.
    soot = np.clip(fbm(S, 3, 5, seed=9) * 1.4 - 0.35, 0, 1)
    ys = np.linspace(1.0, 0.0, S, dtype=np.float32)[:, None]
    alb *= (1 - soot * ys * 0.55)[..., None]

    nrm = height_to_normal(h, 3.0)
    ao = height_to_ao(h, 6, 1.2)
    rough = np.clip(0.22 + (1 - mask) * 0.5 + grime * 0.3 + run * 0.25 +
                    missing * 0.35, 0.05, 1.0)
    metal = np.zeros_like(rough)
    return alb, nrm, ao, rough, metal


def tex_wall_plaster(S=HERO):
    """Painted plaster, blistered and peeling off the lath beneath."""
    coarse = fbm(S, 5, 6, seed=11)
    fine = fbm(S, 40, 4, seed=12)
    peel = fbm(S, 7, 5, seed=13)
    run = streaks(S, 90, seed=14, length=0.85)

    peel_mask = smoothstep(0.52, 0.62, peel)      # where paint has come away
    h = 0.6 + coarse * 0.12 + fine * 0.04 - peel_mask * 0.30 - run * 0.02

    paint = tint(np.clip(0.55 + coarse * 0.3, 0, 1),
                 (0.40, 0.43, 0.38), (0.74, 0.75, 0.68))
    under = tint(np.clip(0.4 + fine * 0.4, 0, 1),
                 (0.30, 0.22, 0.17), (0.52, 0.40, 0.31))
    alb = paint * (1 - peel_mask)[..., None] + under * peel_mask[..., None]
    alb *= (1 - run * 0.5)[..., None]
    soot = np.clip(fbm(S, 3, 5, seed=15) * 1.5 - 0.4, 0, 1)
    ys = np.linspace(1.0, 0.0, S, dtype=np.float32)[:, None]
    alb *= (1 - soot * ys * 0.7)[..., None]

    nrm = height_to_normal(h, 2.6)
    ao = height_to_ao(h, 7, 1.1)
    rough = np.clip(0.62 + peel_mask * 0.25 + run * 0.2 - fine * 0.1, 0.1, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


def tex_concrete(S=STD):
    coarse = fbm(S, 6, 6, seed=21)
    pits = worley(S, 30, seed=22)
    fine = fbm(S, 48, 3, seed=23)
    run = streaks(S, 40, seed=24, length=0.6)

    h = 0.6 + coarse * 0.10 + fine * 0.03 - np.clip(0.28 - pits, 0, 1) * 1.4
    alb = tint(np.clip(0.42 + coarse * 0.30 - fine * 0.1, 0, 1),
               (0.24, 0.24, 0.25), (0.56, 0.56, 0.55))
    alb *= (1 - run * 0.35)[..., None]
    nrm = height_to_normal(h, 2.2)
    ao = height_to_ao(h, 6, 1.3)
    rough = np.clip(0.78 + coarse * 0.15, 0.2, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


# ------------------------------------------------------------------ floors

def tex_floor_lino(S=HERO):
    """Checkerboard linoleum, lifting at the seams, worn through in traffic lanes."""
    mask, u, v, cell = brick_grid(S, 12, 12, mortar=0.02, stagger=0.0)
    check = ((np.floor(np.linspace(0, 12, S, endpoint=False))[None, :] +
              np.floor(np.linspace(0, 12, S, endpoint=False))[:, None]) % 2)
    scuff = fbm(S, 8, 6, seed=31)
    fine = fbm(S, 64, 3, seed=32)
    wear = fbm(S, 3, 4, seed=33)
    run = streaks(S, 30, seed=34, length=0.5)

    lift = smoothstep(0.62, 0.72, fbm(S, 5, 4, seed=35))
    h = mask * 0.45 + 0.5 + fine * 0.02 - lift * 0.22

    light = tint(np.clip(0.55 + scuff * 0.25, 0, 1), (0.44, 0.44, 0.41), (0.76, 0.76, 0.71))
    dark = tint(np.clip(0.35 + scuff * 0.25, 0, 1), (0.17, 0.19, 0.19), (0.36, 0.38, 0.36))
    alb = light * check[..., None] + dark * (1 - check)[..., None]
    alb = alb * mask[..., None] + np.array([0.10, 0.09, 0.09]) * (1 - mask)[..., None]
    alb *= (1 - wear * 0.35)[..., None]
    alb *= (1 - run * 0.4)[..., None]

    nrm = height_to_normal(h, 2.0)
    ao = height_to_ao(h, 5, 1.0)
    # Traffic lanes are polished smooth; the edges stay matte and gritty.
    rough = np.clip(0.5 - wear * 0.32 + (1 - mask) * 0.35 + scuff * 0.18, 0.06, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


def tex_ceiling(S=STD):
    """Sagging acoustic tile with water staining."""
    mask, u, v, cell = brick_grid(S, 6, 6, mortar=0.035, stagger=0.0)
    holes = worley(S, 60, seed=41)
    stain = fbm(S, 3, 5, seed=42)
    sag = fbm(S, 4, 3, seed=43)
    fine = fbm(S, 40, 3, seed=44)

    h = mask * 0.5 + 0.5 - np.clip(0.35 - holes, 0, 1) * 0.8 + fine * 0.02 - sag * 0.05
    missing = (cell % 11 < 1).astype(np.float32)
    h -= missing * 0.6

    alb = tint(np.clip(0.62 + fine * 0.2, 0, 1), (0.46, 0.45, 0.41), (0.78, 0.77, 0.70))
    water = np.clip(stain * 1.7 - 0.55, 0, 1)
    alb = alb * (1 - water * 0.8)[..., None] + \
        np.array([0.38, 0.28, 0.16]) * (water * 0.8)[..., None]
    alb = alb * mask[..., None] + np.array([0.05, 0.05, 0.06]) * (1 - mask)[..., None]
    alb = alb * (1 - missing)[..., None] + np.array([0.03, 0.03, 0.04]) * missing[..., None]

    nrm = height_to_normal(h, 2.2)
    ao = height_to_ao(h, 6, 1.2)
    rough = np.clip(0.85 - water * 0.35, 0.15, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


# ------------------------------------------------------------------ metal

def tex_metal(S=STD):
    """Painted steel: chipped to bare metal, rust bleeding from the chips."""
    brushed = fbm(S, 2, 3, seed=51)
    scratch = np.clip(fbm(S, 96, 2, seed=52) * 1.4 - 0.35, 0, 1)
    chips = smoothstep(0.60, 0.68, fbm(S, 12, 5, seed=53))
    rust = np.clip(fbm(S, 9, 5, seed=54) * 1.5 - 0.45, 0, 1) * chips
    dent = fbm(S, 6, 4, seed=55)

    h = 0.6 + dent * 0.06 - chips * 0.05 + scratch * 0.01
    paint = tint(np.clip(0.5 + brushed * 0.25, 0, 1), (0.24, 0.28, 0.28), (0.46, 0.52, 0.50))
    bare = tint(np.clip(0.45 + scratch * 0.4, 0, 1), (0.38, 0.38, 0.40), (0.68, 0.68, 0.70))
    alb = paint * (1 - chips)[..., None] + bare * chips[..., None]
    alb = alb * (1 - rust)[..., None] + np.array([0.35, 0.16, 0.07]) * rust[..., None]

    nrm = height_to_normal(h, 1.8)
    ao = height_to_ao(h, 5, 0.8)
    rough = np.clip(0.42 + rust * 0.45 - scratch * 0.15 + chips * 0.1, 0.08, 1.0)
    metal = np.clip(chips * 0.85 - rust * 0.7, 0, 1)
    return alb, nrm, ao, rough, metal


def tex_fabric(S=STD):
    """Institutional mattress ticking, stained."""
    weave_x = np.sin(np.linspace(0, np.pi * 2 * 180, S, dtype=np.float32))[None, :]
    weave_y = np.sin(np.linspace(0, np.pi * 2 * 180, S, dtype=np.float32))[:, None]
    weave = (weave_x * weave_y) * 0.5 + 0.5
    stain = fbm(S, 4, 5, seed=61)
    fine = fbm(S, 30, 3, seed=62)
    stripe = (np.floor(np.linspace(0, 16, S, endpoint=False)) % 2)[None, :]

    h = 0.6 + weave * 0.05 + fine * 0.03
    alb = tint(np.clip(0.6 + fine * 0.2, 0, 1), (0.52, 0.50, 0.45), (0.80, 0.79, 0.73))
    alb *= (1 - stripe * 0.12)[..., None]
    dirty = np.clip(stain * 1.6 - 0.5, 0, 1)
    alb = alb * (1 - dirty * 0.75)[..., None] + \
        np.array([0.30, 0.24, 0.16]) * (dirty * 0.75)[..., None]

    nrm = height_to_normal(h, 1.4)
    ao = height_to_ao(h, 4, 0.7)
    rough = np.clip(0.88 - dirty * 0.15, 0.4, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


# ------------------------------------------------------------------- skin

def tex_skin_burn(S=HERO):
    """The Matron. Third-degree burn scarring over drum-tight skin.

    Sampled triplanar, so this has to look right at any orientation and has no
    UV layout to hide behind.
    """
    cells = worley(S, 22, seed=71, kind="f2f1")
    blister = worley(S, 40, seed=72)
    coarse = fbm(S, 6, 6, seed=73)
    fine = fbm(S, 56, 4, seed=74)
    crack = np.clip(1.0 - cells * 5.6, 0, 1)          # split, contracted scar
    keloid = smoothstep(0.45, 0.75, fbm(S, 10, 5, seed=75))

    h = (0.55 + coarse * 0.12 + fine * 0.04
         + keloid * 0.10 + (1 - blister) * 0.05 - crack * 0.35)

    # Waxy, bloodless base; raw and dark red down in the splits.
    base = tint(np.clip(0.45 + coarse * 0.35 + fine * 0.15, 0, 1),
                (0.30, 0.27, 0.25), (0.60, 0.55, 0.51))
    raw = tint(np.clip(0.4 + fine * 0.4, 0, 1),
               (0.14, 0.06, 0.05), (0.31, 0.14, 0.11))
    alb = base * (1 - crack)[..., None] + raw * crack[..., None]
    char = np.clip(fbm(S, 4, 5, seed=76) * 1.6 - 0.40, 0, 1)
    alb *= (1 - char * 0.80)[..., None]
    alb = alb * (1 - keloid * 0.25)[..., None] + \
        np.array([0.62, 0.50, 0.46]) * (keloid * 0.25)[..., None]

    nrm = height_to_normal(h, 3.2)
    ao = height_to_ao(h, 7, 1.5)
    # Weeping in the cracks, dry and dead on the keloid.
    rough = np.clip(0.55 - crack * 0.34 + keloid * 0.2 + char * 0.2, 0.08, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


def tex_skin_child(S=STD):
    """Ward children: grey, waxy, in a filthy gown."""
    coarse = fbm(S, 7, 6, seed=81)
    fine = fbm(S, 48, 4, seed=82)
    veins = np.clip(1.0 - worley(S, 14, seed=83, kind="f2f1") * 2.6, 0, 1)
    soot = np.clip(fbm(S, 5, 5, seed=84) * 1.5 - 0.5, 0, 1)

    h = 0.58 + coarse * 0.08 + fine * 0.03 - veins * 0.06
    alb = tint(np.clip(0.5 + coarse * 0.3 + fine * 0.15, 0, 1),
               (0.40, 0.40, 0.42), (0.74, 0.72, 0.70))
    alb = alb * (1 - veins * 0.4)[..., None] + \
        np.array([0.30, 0.28, 0.36]) * (veins * 0.4)[..., None]
    alb *= (1 - soot * 0.6)[..., None]

    nrm = height_to_normal(h, 2.0)
    ao = height_to_ao(h, 6, 1.1)
    rough = np.clip(0.62 + soot * 0.2 - fine * 0.1, 0.15, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


def tex_rubble(S=STD):
    lumps = worley(S, 16, seed=91)
    coarse = fbm(S, 8, 6, seed=92)
    fine = fbm(S, 50, 3, seed=93)
    h = 0.5 + lumps * 0.3 + coarse * 0.12 + fine * 0.04
    alb = tint(np.clip(0.35 + coarse * 0.35 + lumps * 0.2, 0, 1),
               (0.20, 0.19, 0.18), (0.52, 0.49, 0.45))
    burn = np.clip(fbm(S, 4, 5, seed=94) * 1.4 - 0.45, 0, 1)
    alb *= (1 - burn * 0.65)[..., None]
    nrm = height_to_normal(h, 2.6)
    ao = height_to_ao(h, 7, 1.4)
    rough = np.clip(0.85 + coarse * 0.1, 0.3, 1.0)
    return alb, nrm, ao, rough, np.zeros_like(rough)


MATERIALS = {
    "wall_tile": tex_wall_tile,
    "wall_plaster": tex_wall_plaster,
    "concrete": tex_concrete,
    "floor_lino": tex_floor_lino,
    "ceiling": tex_ceiling,
    "metal": tex_metal,
    "fabric": tex_fabric,
    "skin_burn": tex_skin_burn,
    "skin_child": tex_skin_child,
    "rubble": tex_rubble,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    total = 0
    for name, fn in MATERIALS.items():
        if only and name not in only:
            continue
        alb, nrm, ao, rough, metal = fn()
        pa = os.path.join(OUT, f"{name}_a.png")
        pn = os.path.join(OUT, f"{name}_n.png")
        po = os.path.join(OUT, f"{name}_orm.png")
        save_rgb(pa, alb)
        save_rgb(pn, nrm)
        save_gray_packed(po, ao, rough, metal)
        sz = sum(os.path.getsize(p) for p in (pa, pn, po))
        total += sz
        print(f"  {name:14} {alb.shape[0]:4d}px  {sz/1024/1024:6.2f} MB")
    print(f"\ntextures total {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    main()
