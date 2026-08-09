#!/usr/bin/env python3
"""Build every 3D asset in the game.

Characters are SDF sculpts polygonised with marching cubes and auto-skinned.
Hard-surface props are assembled from boxes and cylinders directly, because
running a smooth-blended field over a steel locker just rounds off everything
that made it read as a steel locker.

Run: python3 tools/gen_meshes.py
Out: android/app/src/main/assets/mesh/*.hmsh
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from meshlib import (  # noqa: E402
    SDF, Clip, Mesh, Skeleton, decimate_grid, fbm3, polygonise, report,
    sd_box, sd_capsule, sd_sphere, weld,
)

OUT = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src",
                   "main", "assets", "mesh")


# ============================================================ hard surface

def box_mesh(center, half, uv_scale=1.0):
    """Axis-aligned box with per-face normals and planar UVs."""
    c = np.asarray(center, np.float32)
    h = np.asarray(half, np.float32)
    faces = [
        ((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0)),
        ((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)),
        ((0, 1, 0), (0, 0, 1)), ((0, -1, 0), (0, 0, 1)),
    ]
    V, N, U, I = [], [], [], []
    for n, up in faces:
        n = np.array(n, np.float32)
        up = np.array(up, np.float32)
        right = np.cross(up, n)
        ext_r = float(np.abs(right) @ h)
        ext_u = float(np.abs(up) @ h)
        origin = c + n * float(np.abs(n) @ h)
        base = len(V)
        for sr, su in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            V.append(origin + right * (sr * ext_r) + up * (su * ext_u))
            N.append(n)
            U.append([(sr * ext_r) * uv_scale, (su * ext_u) * uv_scale])
        I += [base, base + 1, base + 2, base, base + 2, base + 3]
    return (np.array(V, np.float32), np.array(N, np.float32),
            np.array(U, np.float32), np.array(I, np.uint32))


def cyl_mesh(a, b, r, seg=12, uv_scale=1.0):
    a = np.asarray(a, np.float32)
    b = np.asarray(b, np.float32)
    axis = b - a
    ln = float(np.linalg.norm(axis)) or 1e-6
    axis = axis / ln
    tmp = np.array([0, 1, 0], np.float32)
    if abs(float(axis @ tmp)) > 0.95:
        tmp = np.array([1, 0, 0], np.float32)
    u = np.cross(axis, tmp)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)

    V, N, U, I = [], [], [], []
    for i in range(seg + 1):
        th = i / seg * 2 * np.pi
        d = u * np.cos(th) + v * np.sin(th)
        for end, p in ((0, a), (1, b)):
            V.append(p + d * r)
            N.append(d)
            U.append([i / seg * np.pi * 2 * r * uv_scale, end * ln * uv_scale])
    for i in range(seg):
        o = i * 2
        I += [o, o + 1, o + 3, o, o + 3, o + 2]
    # Caps
    for end, p, nrm in ((0, a, -axis), (1, b, axis)):
        centre = len(V)
        V.append(p)
        N.append(nrm)
        U.append([0, 0])
        for i in range(seg + 1):
            th = i / seg * 2 * np.pi
            d = u * np.cos(th) + v * np.sin(th)
            V.append(p + d * r)
            N.append(nrm)
            U.append([np.cos(th) * r * uv_scale, np.sin(th) * r * uv_scale])
        for i in range(seg):
            if end:
                I += [centre, centre + 1 + i, centre + 2 + i]
            else:
                I += [centre, centre + 2 + i, centre + 1 + i]
    return (np.array(V, np.float32), np.array(N, np.float32),
            np.array(U, np.float32), np.array(I, np.uint32))


def combine(parts):
    V, N, U, I = [], [], [], []
    off = 0
    for v, n, u, i in parts:
        V.append(v)
        N.append(n)
        U.append(u)
        I.append(i + off)
        off += len(v)
    return (np.concatenate(V), np.concatenate(N),
            np.concatenate(U), np.concatenate(I).astype(np.uint32))


# ================================================================= the Matron
#
# Two point three metres of malnourished vertical wrongness. The proportions do
# the work: arms that reach past the knees, a neck too long for the head it
# carries, and a ribcage you can count from across a room.

def sd_ellipsoid_socket(p, c):
    """Eye socket: wide, shallow-topped, and driven back into the skull."""
    q = (p - np.asarray(c, np.float32)) / np.array([0.052, 0.040, 0.062], np.float32)
    return (np.linalg.norm(q, axis=-1) - 1.0) * 0.045


def matron_sdf():
    f = SDF()

    # Legs — thin, with knees that read as knobs rather than joints.
    for s in (-1, 1):
        x = 0.115 * s
        f.union(lambda p, x=x: sd_capsule(p, (x, 1.05, 0), (x * 1.12, 0.60, 0.01), 0.085, 0.062), 0.06)
        f.union(lambda p, x=x: sd_capsule(p, (x * 1.12, 0.62, 0.01), (x * 1.1, 0.12, -0.01), 0.062, 0.045), 0.05)
        f.union(lambda p, x=x: sd_capsule(p, (x * 1.1, 0.07, -0.02), (x * 1.1, 0.045, 0.15), 0.05, 0.035), 0.04)
        f.union(lambda p, x=x: sd_sphere(p, (x * 1.12, 0.61, 0.015), 0.072), 0.05)

    # Pelvis and torso, tapering the wrong way — wide ribs, narrow hips.
    f.union(lambda p: sd_capsule(p, (0, 1.02, 0), (0, 1.30, 0), 0.135, 0.145), 0.09)
    f.union(lambda p: sd_capsule(p, (0, 1.30, 0), (0, 1.74, -0.01), 0.145, 0.175), 0.10)

    # Ribs. Individual bands so the torso is not a smooth tube. The blend has to
    # stay tight or marching cubes averages them straight back into one.
    for i in range(8):
        y = 1.40 + i * 0.050
        rr = 0.182 - abs(i - 3.5) * 0.013
        f.union(lambda p, y=y, rr=rr: sd_capsule(
            p, (-0.155, y, 0.03), (0.155, y, 0.03), rr * 0.46), 0.028)
    # Sternum hollow and the pit under the ribs.
    f.cut(lambda p: sd_capsule(p, (0, 1.44, 0.15), (0, 1.70, 0.14), 0.038), 0.05)

    # Collar and shoulders.
    f.union(lambda p: sd_capsule(p, (-0.20, 1.755, 0), (0.20, 1.755, 0), 0.062), 0.06)

    # Arms: upper and fore both long, ending in hands at mid-shin height.
    for s in (-1, 1):
        f.union(lambda p, s=s: sd_capsule(p, (0.20 * s, 1.75, 0),
                                          (0.285 * s, 1.28, 0.02), 0.058, 0.044), 0.055)
        f.union(lambda p, s=s: sd_capsule(p, (0.285 * s, 1.29, 0.02),
                                          (0.325 * s, 0.84, 0.05), 0.044, 0.032), 0.045)
        f.union(lambda p, s=s: sd_sphere(p, (0.285 * s, 1.285, 0.02), 0.052), 0.04)
        # Hand: a palm and five long fingers, not a mitten.
        f.union(lambda p, s=s: sd_capsule(p, (0.325 * s, 0.845, 0.05),
                                          (0.335 * s, 0.755, 0.055), 0.038, 0.030), 0.035)
        for k in range(5):
            fx = 0.335 * s + (k - 2) * 0.019 * s
            f.union(lambda p, fx=fx, k=k: sd_capsule(
                p, (fx, 0.760, 0.055),
                (fx * 1.02, 0.760 - (0.115 - abs(k - 2) * 0.016), 0.075 + k * 0.002),
                0.014, 0.008), 0.018)

    # Neck: too long, and it leans.
    f.union(lambda p: sd_capsule(p, (0, 1.755, -0.01), (0.01, 1.985, -0.035), 0.052, 0.045), 0.05)

    # Skull. Larger than a head has any business being on that neck, and
    # elongated backward — a small head vanishes entirely at torchlight range.
    f.union(lambda p: sd_capsule(p, (0.012, 1.995, -0.075), (0.016, 2.105, 0.000), 0.104, 0.092), 0.045)
    # Brow ridge and cheekbones, so the face has structure to catch light on.
    f.union(lambda p: sd_capsule(p, (-0.062, 2.072, 0.052), (0.090, 2.072, 0.052), 0.026), 0.030)
    f.union(lambda p: sd_capsule(p, (-0.055, 1.998, 0.045), (0.083, 1.998, 0.045), 0.024), 0.030)
    # Maxilla and a jaw hung open. The gap is the whole point of the face.
    f.union(lambda p: sd_capsule(p, (0.014, 2.020, 0.030), (0.014, 1.988, 0.062), 0.058, 0.040), 0.030)
    f.union(lambda p: sd_capsule(p, (-0.050, 1.930, 0.010), (0.078, 1.930, 0.010), 0.028), 0.028)
    f.union(lambda p: sd_capsule(p, (-0.050, 1.930, 0.010), (0.014, 1.916, 0.052), 0.026), 0.028)
    f.union(lambda p: sd_capsule(p, (0.078, 1.930, 0.010), (0.014, 1.916, 0.052), 0.026), 0.028)

    # Eye sockets, cut deep and wide. There is nothing in them; the fire took them.
    for s in (-1, 1):
        f.cut(lambda p, s=s: sd_ellipsoid_socket(p, (0.014 + 0.047 * s, 2.040, 0.062)), 0.008)
    # Nasal cavity, and the dark between the teeth.
    f.cut(lambda p: sd_capsule(p, (0.014, 2.030, 0.070), (0.014, 1.995, 0.078), 0.020, 0.026), 0.008)
    f.cut(lambda p: sd_box(p, (0.014, 1.958, 0.048), (0.036, 0.014, 0.034)), 0.006)
    # Temples hollowed out.
    for s in (-1, 1):
        f.cut(lambda p, s=s: sd_sphere(p, (0.014 + 0.098 * s, 2.055, 0.010), 0.048), 0.030)

    # Uniform. The apron is a flat panel and the skirt hangs from the hips only,
    # so the ribcage stays visible above it instead of being absorbed.
    f.union(lambda p: sd_box(p, (0, 1.40, 0.152), (0.132, 0.20, 0.016), 0.012), 0.022)
    f.union(lambda p: sd_box(p, (0, 1.63, 0.150), (0.052, 0.09, 0.013), 0.010), 0.020)
    f.union(lambda p: sd_capsule(p, (0, 1.10, 0.005), (0, 0.70, 0.015), 0.168, 0.212), 0.035)
    # Ragged hem: erode the surface (positive displacement) as height falls below
    # the hem line, gated to a radius outside the legs so it cannot eat them.
    f.bump(lambda p: (np.clip((0.80 - p[:, 1]) / 0.10, 0.0, 1.0)
                      * (np.hypot(p[:, 0], p[:, 2]) > 0.145)
                      * np.maximum(fbm3(p, 2, 16.0, seed=15), 0.0) * 0.055))

    # Burned skin: blistering, and rib/vertebra relief that noise alone gives.
    f.bump(lambda p: fbm3(p, 4, 12.0, seed=3) * 0.0075)
    f.bump(lambda p: np.maximum(fbm3(p, 2, 34.0, seed=9), 0.0) * 0.0035)
    return f


def matron_skeleton():
    s = Skeleton()
    s.add("root", (0, 0.00, 0), (0, 0.10, 0))
    s.add("hips", (0, 1.02, 0), (0, 1.30, 0), "root")
    s.add("spine", (0, 1.30, 0), (0, 1.52, 0), "hips")
    s.add("chest", (0, 1.52, 0), (0, 1.755, 0), "spine")
    s.add("neck", (0, 1.755, -0.01), (0, 1.985, -0.03), "chest")
    s.add("head", (0, 1.985, -0.03), (0, 2.14, -0.01), "neck")
    for tag, s_ in (("L", 1), ("R", -1)):
        s.add(f"clav.{tag}", (0.03 * s_, 1.755, 0), (0.20 * s_, 1.755, 0), "chest")
        s.add(f"arm.{tag}", (0.20 * s_, 1.75, 0), (0.285 * s_, 1.29, 0.02), f"clav.{tag}")
        s.add(f"fore.{tag}", (0.285 * s_, 1.29, 0.02), (0.325 * s_, 0.845, 0.05), f"arm.{tag}")
        s.add(f"hand.{tag}", (0.325 * s_, 0.845, 0.05), (0.335 * s_, 0.66, 0.07), f"fore.{tag}")
    for tag, s_ in (("L", 1), ("R", -1)):
        s.add(f"thigh.{tag}", (0.115 * s_, 1.05, 0), (0.128 * s_, 0.61, 0.01), "hips")
        s.add(f"shin.{tag}", (0.128 * s_, 0.61, 0.01), (0.126 * s_, 0.12, -0.01), f"thigh.{tag}")
        s.add(f"foot.{tag}", (0.126 * s_, 0.10, -0.02), (0.126 * s_, 0.045, 0.15), f"shin.{tag}")
    return s


def matron_clips():
    """Five clips. The walk is the one that matters — the player watches it for
    an hour, so it gets a limp and an asymmetric arm swing."""
    clips = []

    idle = Clip("idle", 5.0, fps=24)
    for t, a in ((0, 0), (2.5, 1), (5.0, 0)):
        idle.key("chest", t, (2 + a * 2, a * 3, 0))
        idle.key("neck", t, (-3 - a * 3, a * 5, a * 2))
        idle.key("head", t, (a * 4, -a * 6, 0))
        idle.key("spine", t, (a * 1.5, 0, 0))
        for tag, s_ in (("L", 1), ("R", -1)):
            idle.key(f"arm.{tag}", t, (a * 3, 0, s_ * (2 + a * 2)))
            idle.key(f"fore.{tag}", t, (a * 5 + 4, 0, 0))
    clips.append(idle)

    # Walk: 1.6s cycle. Heavy on the right leg, head sweeping to listen.
    walk = Clip("walk", 1.6, fps=30)
    steps = [(0.0, 1), (0.4, 0), (0.8, -1), (1.2, 0), (1.6, 1)]
    for t, ph in steps:
        walk.key("hips", t, (0, ph * 3, abs(ph) * 2), (0, -abs(ph) * 0.02, 0))
        walk.key("spine", t, (4, -ph * 2, 0))
        walk.key("chest", t, (3, ph * 3, 0))
        walk.key("neck", t, (-6, ph * 8, 0))
        walk.key("head", t, (2, ph * 10, ph * 3))
        walk.key("thigh.L", t, (ph * 26, 0, 0))
        walk.key("shin.L", t, (max(0, -ph * 34) + 6, 0, 0))
        walk.key("foot.L", t, (-ph * 10, 0, 0))
        walk.key("thigh.R", t, (-ph * 22, 0, 0))
        walk.key("shin.R", t, (max(0, ph * 40) + 10, 0, 0))   # the limp
        walk.key("foot.R", t, (ph * 8, 0, 0))
        walk.key("arm.L", t, (-ph * 14, 0, 4))
        walk.key("fore.L", t, (8 + abs(ph) * 6, 0, 0))
        walk.key("arm.R", t, (ph * 18, 0, -4))
        walk.key("fore.R", t, (14 + abs(ph) * 10, 0, 0))
    clips.append(walk)

    # Listen: she stops dead and cants her head. Sold entirely by the freeze.
    listen = Clip("listen", 3.2, fps=24, loop=True)
    for t, a in ((0, 0), (0.35, 1), (1.6, 1), (2.0, -1), (3.2, 0)):
        listen.key("chest", t, (2, a * 6, 0))
        listen.key("neck", t, (-4, a * 16, a * 22))
        listen.key("head", t, (a * 6, a * 20, a * 26))
        listen.key("arm.L", t, (-6, 0, 6))
        listen.key("arm.R", t, (-6, 0, -6))
    clips.append(listen)

    # Chase: long reaching strides, spine pitched forward, arms out.
    run = Clip("chase", 0.72, fps=30)
    for t, ph in ((0.0, 1), (0.18, 0), (0.36, -1), (0.54, 0), (0.72, 1)):
        run.key("hips", t, (14, ph * 5, 0), (0, abs(ph) * 0.03, 0))
        run.key("spine", t, (14, -ph * 5, 0))
        run.key("chest", t, (10, ph * 6, 0))
        run.key("neck", t, (-18, ph * 4, 0))
        run.key("head", t, (-8, ph * 5, 0))
        run.key("thigh.L", t, (ph * 52, 0, 0))
        run.key("shin.L", t, (max(0, -ph * 62) + 12, 0, 0))
        run.key("thigh.R", t, (-ph * 52, 0, 0))
        run.key("shin.R", t, (max(0, ph * 62) + 12, 0, 0))
        run.key("arm.L", t, (-32 - ph * 34, 0, 14))
        run.key("fore.L", t, (-58, 0, 0))
        run.key("arm.R", t, (-32 + ph * 34, 0, -14))
        run.key("fore.R", t, (-58, 0, 0))
    clips.append(run)

    # Scream: everything opens at once, then holds.
    # Scream: she pitches forward from the hips, head snaps back, arms come up
    # and out in front of her. Rotating a downward-hanging arm by a negative X
    # angle swings it forward; going past about -90 puts it over the head, which
    # reads as celebration rather than threat.
    scream = Clip("scream", 1.5, fps=30, loop=False)
    for t, a in ((0.0, 0), (0.10, 1), (0.9, 1), (1.5, 0.5)):
        scream.key("hips", t, (a * 6, 0, 0))
        scream.key("spine", t, (a * 14, 0, 0))
        scream.key("chest", t, (a * 12, 0, 0))
        scream.key("neck", t, (-a * 26, 0, 0))
        scream.key("head", t, (-a * 22, 0, 0))
        scream.key("arm.L", t, (-a * 72, 0, a * 34))
        scream.key("fore.L", t, (-a * 46, 0, a * 16))
        scream.key("arm.R", t, (-a * 72, 0, -a * 34))
        scream.key("fore.R", t, (-a * 46, 0, -a * 16))
        scream.key("hand.L", t, (0, 0, a * 24))
        scream.key("hand.R", t, (0, 0, -a * 24))
    clips.append(scream)

    return clips


def matron_head_sdf():
    """The skull, sculpted on its own so it can be polygonised at ~2 mm.

    A single field over the whole 2.2 m body gives ~10 mm voxels, at which an eye
    socket is five voxels across and the smooth-min blends round it back into a
    bald ellipsoid. Faces are what horror is made of, so the head is a separate
    closed solid that interpenetrates the body's neck stub.
    """
    f = SDF()
    # Cranium: long front-to-back, narrow across, temples pinched.
    f.union(lambda p: sd_capsule(p, (0.012, 1.995, -0.075), (0.016, 2.105, 0.000), 0.104, 0.092), 0.040)
    f.union(lambda p: sd_capsule(p, (0.014, 2.050, -0.02), (0.014, 2.050, 0.058), 0.086, 0.070), 0.035)
    # Neck stub, so the head is closed where it meets the body.
    f.union(lambda p: sd_capsule(p, (0, 1.86, -0.03), (0.012, 1.99, -0.05), 0.050, 0.058), 0.045)

    # Brow, cheekbones, maxilla.
    f.union(lambda p: sd_capsule(p, (-0.058, 2.078, 0.056), (0.086, 2.078, 0.056), 0.024), 0.022)
    f.union(lambda p: sd_capsule(p, (-0.060, 2.000, 0.040), (0.088, 2.000, 0.040), 0.026), 0.024)
    f.union(lambda p: sd_capsule(p, (0.014, 2.024, 0.030), (0.014, 1.986, 0.066), 0.056, 0.038), 0.024)

    # Mandible: a U hung open, hinged back at the ears.
    f.union(lambda p: sd_capsule(p, (-0.062, 1.998, -0.010), (-0.052, 1.918, 0.018), 0.021), 0.020)
    f.union(lambda p: sd_capsule(p, (0.090, 1.998, -0.010), (0.080, 1.918, 0.018), 0.021), 0.020)
    f.union(lambda p: sd_capsule(p, (-0.052, 1.918, 0.018), (0.014, 1.902, 0.062), 0.020), 0.020)
    f.union(lambda p: sd_capsule(p, (0.080, 1.918, 0.018), (0.014, 1.902, 0.062), 0.020), 0.020)

    # Eye sockets: wide, deep, and undercut so they stay black from every angle.
    for s in (-1, 1):
        cx = 0.014 + 0.048 * s
        f.cut(lambda p, cx=cx: sd_ellipsoid_socket(p, (cx, 2.042, 0.064)), 0.006)
        f.cut(lambda p, cx=cx: sd_sphere(p, (cx, 2.038, 0.030), 0.038), 0.010)
    # Nasal aperture.
    f.cut(lambda p: sd_capsule(p, (0.014, 2.032, 0.074), (0.014, 1.992, 0.082), 0.017, 0.024), 0.006)
    # Temples.
    for s in (-1, 1):
        f.cut(lambda p, s=s: sd_sphere(p, (0.014 + 0.100 * s, 2.058, 0.012), 0.046), 0.026)

    # Teeth. At 2 mm these actually survive, and they are what sells the jaw.
    for k in range(9):
        t = (k - 4) / 4.0
        tx = 0.014 + t * 0.050
        tz = 0.062 - abs(t) * 0.030
        if k % 4 != 3:
            f.union(lambda p, tx=tx, tz=tz: sd_capsule(
                p, (tx, 1.988, tz), (tx, 1.968, tz), 0.0085), 0.004)
        if k % 3 != 2:
            f.union(lambda p, tx=tx, tz=tz: sd_capsule(
                p, (tx, 1.916, tz), (tx, 1.934, tz), 0.0080), 0.004)

    # Burn scarring, finer than the body's because it is seen closer.
    f.bump(lambda p: fbm3(p, 4, 34.0, seed=31) * 0.0028)
    f.bump(lambda p: np.maximum(fbm3(p, 2, 76.0, seed=37), 0.0) * 0.0014)
    return f


def build_matron():
    v, n, i = polygonise(matron_sdf(), (-0.55, -0.05, -0.44), (0.55, 2.26, 0.48), res=232)
    v, n, i = weld(v, n, i)
    v, n, i = decimate_grid(v, n, i, 0.0085)

    hv, hn, hi = polygonise(matron_head_sdf(),
                            (-0.13, 1.85, -0.21), (0.17, 2.20, 0.13), res=190)
    hv, hn, hi = weld(hv, hn, hi)
    hv, hn, hi = decimate_grid(hv, hn, hi, 0.0028)

    verts = np.concatenate([v, hv])
    norms = np.concatenate([n, hn])
    idx = np.concatenate([i.reshape(-1), hi.reshape(-1) + len(v)])

    skel = matron_skeleton()
    bi, bw = skel.skin(verts, falloff=3.0)
    return Mesh("matron", verts, norms, idx, bone_idx=bi, bone_w=bw,
                skel=skel, clips=matron_clips())


# ================================================================ ward child
#
# Crawls. Never stands. Harmless, and the most upsetting thing in the game.

def child_sdf():
    f = SDF()
    # Torso along +Z, low to the ground.
    f.union(lambda p: sd_capsule(p, (0, 0.30, -0.18), (0, 0.33, 0.16), 0.115, 0.10), 0.06)
    f.union(lambda p: sd_capsule(p, (0, 0.31, 0.16), (0.01, 0.34, 0.28), 0.075, 0.06), 0.05)
    # Head, hanging low.
    f.union(lambda p: sd_capsule(p, (0.01, 0.33, 0.30), (0.01, 0.31, 0.40), 0.082, 0.072), 0.05)
    for s in (-1, 1):
        f.cut(lambda p, s=s: sd_sphere(p, (0.01 + 0.032 * s, 0.335, 0.435), 0.030), 0.010)
    f.cut(lambda p: sd_box(p, (0.01, 0.285, 0.42), (0.026, 0.007, 0.028)), 0.006)
    # Arms and legs, planted.
    for s in (-1, 1):
        f.union(lambda p, s=s: sd_capsule(p, (0.09 * s, 0.32, 0.20), (0.15 * s, 0.16, 0.30), 0.040, 0.030), 0.04)
        f.union(lambda p, s=s: sd_capsule(p, (0.15 * s, 0.16, 0.30), (0.17 * s, 0.03, 0.36), 0.030, 0.026), 0.035)
        f.union(lambda p, s=s: sd_capsule(p, (0.08 * s, 0.30, -0.14), (0.14 * s, 0.17, -0.22), 0.048, 0.036), 0.04)
        f.union(lambda p, s=s: sd_capsule(p, (0.14 * s, 0.17, -0.22), (0.15 * s, 0.03, -0.14), 0.036, 0.028), 0.035)
    # Hospital gown, torn.
    f.union(lambda p: sd_capsule(p, (0, 0.29, -0.20), (0, 0.30, 0.12), 0.135, 0.12), 0.06)
    f.bump(lambda p: fbm3(p, 4, 16.0, seed=21) * 0.006)
    return f


def child_skeleton():
    s = Skeleton()
    s.add("root", (0, 0, 0), (0, 0.1, 0))
    s.add("hips", (0, 0.30, -0.18), (0, 0.32, 0.02), "root")
    s.add("spine", (0, 0.32, 0.02), (0, 0.33, 0.18), "hips")
    s.add("neck", (0, 0.33, 0.18), (0.01, 0.33, 0.30), "spine")
    s.add("head", (0.01, 0.33, 0.30), (0.01, 0.31, 0.42), "neck")
    for tag, s_ in (("L", 1), ("R", -1)):
        s.add(f"arm.{tag}", (0.09 * s_, 0.32, 0.20), (0.15 * s_, 0.16, 0.30), "spine")
        s.add(f"fore.{tag}", (0.15 * s_, 0.16, 0.30), (0.17 * s_, 0.03, 0.36), f"arm.{tag}")
        s.add(f"leg.{tag}", (0.08 * s_, 0.30, -0.14), (0.14 * s_, 0.17, -0.22), "hips")
        s.add(f"shin.{tag}", (0.14 * s_, 0.17, -0.22), (0.15 * s_, 0.03, -0.14), f"leg.{tag}")
    return s


def child_clips():
    clips = []
    idle = Clip("idle", 4.0, fps=24)
    for t, a in ((0, 0), (1.3, 1), (2.6, -0.4), (4.0, 0)):
        idle.key("spine", t, (a * 3, 0, 0))
        idle.key("neck", t, (a * 6, a * 9, 0))
        idle.key("head", t, (a * 5, a * 14, a * 6))
    clips.append(idle)

    crawl = Clip("crawl", 1.9, fps=30)
    for t, ph in ((0.0, 1), (0.475, 0), (0.95, -1), (1.425, 0), (1.9, 1)):
        crawl.key("hips", t, (0, ph * 6, 0), (0, abs(ph) * 0.015, 0))
        crawl.key("spine", t, (ph * 5, -ph * 7, 0))
        crawl.key("neck", t, (6, ph * 6, 0))
        crawl.key("head", t, (4, ph * 8, 0))
        crawl.key("arm.L", t, (-ph * 34, 0, 0))
        crawl.key("fore.L", t, (max(0, ph * 26), 0, 0))
        crawl.key("arm.R", t, (ph * 34, 0, 0))
        crawl.key("fore.R", t, (max(0, -ph * 26), 0, 0))
        crawl.key("leg.L", t, (ph * 26, 0, 0))
        crawl.key("leg.R", t, (-ph * 26, 0, 0))
    clips.append(crawl)

    # The shriek. This is the alarm that brings the Matron.
    shriek = Clip("shriek", 1.4, fps=30, loop=False)
    for t, a in ((0.0, 0), (0.10, 1), (0.85, 1), (1.4, 0.4)):
        shriek.key("spine", t, (-a * 16, 0, 0))
        shriek.key("neck", t, (-a * 34, 0, 0))
        shriek.key("head", t, (-a * 30, 0, 0))
        shriek.key("arm.L", t, (-a * 20, 0, a * 24))
        shriek.key("arm.R", t, (-a * 20, 0, -a * 24))
    clips.append(shriek)
    return clips


def build_child():
    v, n, i = polygonise(child_sdf(), (-0.32, -0.03, -0.36), (0.32, 0.52, 0.50), res=112)
    v, n, i = weld(v, n, i)
    v, n, i = decimate_grid(v, n, i, 0.009)
    skel = child_skeleton()
    bi, bw = skel.skin(v, falloff=3.0)
    return Mesh("child", v, n, i, bone_idx=bi, bone_w=bw, skel=skel, clips=child_clips())


# ==================================================================== props

def build_bed():
    U = 1.0
    parts = [
        box_mesh((0, 0.52, 0), (0.46, 0.06, 1.00), U),          # mattress
        box_mesh((0, 0.42, 0), (0.44, 0.05, 0.98), U),          # frame
        box_mesh((0, 0.72, -1.02), (0.46, 0.26, 0.03), U),      # headboard
        box_mesh((0, 0.62, 1.02), (0.44, 0.16, 0.03), U),       # footboard
    ]
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(cyl_mesh((sx * 0.40, 0.0, sz * 0.92),
                                  (sx * 0.40, 0.40, sz * 0.92), 0.022, 8, U))
    for sx in (-1, 1):
        parts.append(box_mesh((sx * 0.47, 0.66, 0), (0.02, 0.14, 0.55), U))   # side rails
    return Mesh("bed", *_prop(parts))


def build_locker():
    U = 1.0
    parts = [
        box_mesh((0, 0.90, 0), (0.30, 0.90, 0.24), U),
        box_mesh((0, 0.90, 0.245), (0.28, 0.86, 0.01), U),      # door face
        box_mesh((0.20, 0.92, 0.26), (0.02, 0.06, 0.012), U),   # handle
    ]
    for i in range(4):
        parts.append(box_mesh((0, 1.42 + i * 0.045, 0.256), (0.16, 0.008, 0.006), U))
    return Mesh("locker", *_prop(parts))


def build_gurney():
    U = 1.0
    parts = [
        box_mesh((0, 0.78, 0), (0.36, 0.05, 0.92), U),
        box_mesh((0, 0.70, 0), (0.34, 0.04, 0.90), U),
    ]
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(cyl_mesh((sx * 0.30, 0.10, sz * 0.80),
                                  (sx * 0.30, 0.68, sz * 0.80), 0.018, 8, U))
            parts.append(cyl_mesh((sx * 0.30, 0.05, sz * 0.80 - 0.03),
                                  (sx * 0.30, 0.05, sz * 0.80 + 0.03), 0.055, 10, U))
    parts.append(box_mesh((0, 0.92, -0.86), (0.34, 0.14, 0.02), U))
    return Mesh("gurney", *_prop(parts))


def build_ivstand():
    U = 1.0
    parts = [cyl_mesh((0, 0.04, 0), (0, 1.72, 0), 0.014, 8, U),
             cyl_mesh((0, 1.70, -0.10), (0, 1.70, 0.10), 0.010, 6, U),
             box_mesh((0, 1.55, 0.10), (0.05, 0.11, 0.03), U)]
    for i in range(4):
        th = i / 4 * 2 * np.pi
        parts.append(cyl_mesh((0, 0.03, 0),
                              (np.cos(th) * 0.22, 0.02, np.sin(th) * 0.22), 0.010, 6, U))
    return Mesh("ivstand", *_prop(parts))


def build_wheelchair():
    U = 1.0
    parts = [
        box_mesh((0, 0.50, 0), (0.26, 0.03, 0.24), U),
        box_mesh((0, 0.76, -0.24), (0.26, 0.24, 0.03), U),
    ]
    for sx in (-1, 1):
        parts.append(cyl_mesh((sx * 0.30, 0.30, 0.0), (sx * 0.33, 0.30, 0.0), 0.30, 16, U))
        parts.append(cyl_mesh((sx * 0.22, 0.08, 0.30), (sx * 0.24, 0.08, 0.30), 0.08, 10, U))
        parts.append(cyl_mesh((sx * 0.26, 0.52, -0.24), (sx * 0.26, 0.98, -0.30), 0.016, 8, U))
    return Mesh("wheelchair", *_prop(parts))


def build_lamp():
    U = 1.0
    parts = [cyl_mesh((0, 0.0, 0), (0, 0.30, 0), 0.008, 6, U),
             box_mesh((0, -0.04, 0), (0.30, 0.05, 0.10), U),
             box_mesh((0, -0.10, 0), (0.28, 0.02, 0.08), U)]
    return Mesh("lamp", *_prop(parts))


def build_debris():
    """Rubble heap. SDF here, because rubble should be lumpy."""
    f = SDF()
    rng = np.random.default_rng(77)
    for _ in range(18):
        c = (rng.uniform(-0.7, 0.7), rng.uniform(0.02, 0.34), rng.uniform(-0.7, 0.7))
        f.union(lambda p, c=c, r=rng.uniform(0.10, 0.24): sd_box(
            p, c, (r, r * rng.uniform(0.3, 0.7), r), 0.02), 0.05)
    f.bump(lambda p: fbm3(p, 3, 9.0, seed=5) * 0.02)
    v, n, i = polygonise(f, (-1.0, -0.02, -1.0), (1.0, 0.55, 1.0), res=88)
    v, n, i = weld(v, n, i)
    v, n, i = decimate_grid(v, n, i, 0.028)
    return Mesh("debris", v, n, i)


def _prop(parts):
    v, n, u, i = combine(parts)
    return v, n, i, u


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    builders = [
        ("matron", build_matron), ("child", build_child),
        ("bed", build_bed), ("locker", build_locker), ("gurney", build_gurney),
        ("ivstand", build_ivstand), ("wheelchair", build_wheelchair),
        ("lamp", build_lamp), ("debris", build_debris),
    ]
    total = 0
    for name, fn in builders:
        if only and name not in only:
            continue
        mesh = fn()
        path = os.path.join(OUT, f"{name}.hmsh")
        mesh.write(path)
        report(mesh, path)
        total += os.path.getsize(path)
    print(f"\nmeshes total {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    main()
