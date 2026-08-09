#!/usr/bin/env python3
"""Procedural PBR texture authoring.

Every map is generated tileable: the value-noise lattice wraps, so a wall can
repeat across a 30 m corridor without a visible seam.

Each material produces three maps:
  albedo  sRGB base colour
  normal  tangent-space, derived from the same height field that drove the albedo
  orm     r = ambient occlusion, g = roughness, b = metallic

Deriving the normal and AO from the height field that also modulates the albedo
is what keeps them consistent — grime sits in the cracks because the crack is a
real dip in the height field, not a separately invented pattern.
"""
import numpy as np
from PIL import Image

# ------------------------------------------------------------------- noise


def _lattice(res, size, rng):
    """Random lattice upsampled with wraparound — inherently tileable."""
    g = rng.random((res, res)).astype(np.float32)
    ys = np.linspace(0, res, size, endpoint=False, dtype=np.float32)
    xs = np.linspace(0, res, size, endpoint=False, dtype=np.float32)
    y0 = np.floor(ys).astype(int) % res
    x0 = np.floor(xs).astype(int) % res
    y1 = (y0 + 1) % res
    x1 = (x0 + 1) % res
    fy = (ys - np.floor(ys))[:, None]
    fx = (xs - np.floor(xs))[None, :]
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = g[np.ix_(y0, x0)]
    b = g[np.ix_(y0, x1)]
    c = g[np.ix_(y1, x0)]
    d = g[np.ix_(y1, x1)]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(size, base=4, octaves=6, seed=0, gain=0.5, lac=2):
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size), np.float32)
    amp, res, norm = 1.0, base, 0.0
    for _ in range(octaves):
        if res > size:
            break
        out += _lattice(int(res), size, rng) * amp
        norm += amp
        amp *= gain
        res *= lac
    return out / max(norm, 1e-6)


def worley(size, cells=8, seed=0, kind="f1"):
    """Tileable Worley/cellular noise. Cracks, cobbles, and cell structure.

    Only the pixel's own cell and its eight neighbours are considered, so cost
    is nine passes over the image rather than every-pixel-against-every-feature
    (which at 1024px is a 5 GB intermediate and an instant OOM).
    """
    rng = np.random.default_rng(seed)
    fp = rng.random((cells, cells, 2)).astype(np.float32)

    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    u = xs / size * cells
    v = ys / size * cells
    fu = np.floor(u)
    fv = np.floor(v)
    cx = fu.astype(np.int32) % cells
    cy = fv.astype(np.int32) % cells

    best1 = np.full((size, size), 9.0, np.float32)
    best2 = np.full((size, size), 9.0, np.float32)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            nx = (cx + ox) % cells
            ny = (cy + oy) % cells
            px = fu + ox + fp[ny, nx, 0]
            py = fv + oy + fp[ny, nx, 1]
            d = np.hypot(u - px, v - py)
            better = d < best1
            best2 = np.where(better, best1, np.minimum(best2, d))
            best1 = np.minimum(best1, d)

    r = (best2 - best1) if kind == "f2f1" else best1
    return norm01(r).astype(np.float32)


def norm01(a):
    lo, hi = float(a.min()), float(a.max())
    return (a - lo) / max(hi - lo, 1e-6)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / max(e1 - e0, 1e-6), 0, 1)
    return t * t * (3 - 2 * t)


# --------------------------------------------------------------- patterns

def brick_grid(size, cols, rows, mortar=0.03, stagger=0.5):
    """Returns (mask, u, v, cell_id) — mask 0 in the mortar, 1 on the face."""
    ys = np.linspace(0, rows, size, endpoint=False, dtype=np.float32)
    xs = np.linspace(0, cols, size, endpoint=False, dtype=np.float32)
    Y, X = np.meshgrid(ys, xs, indexing="ij")
    row = np.floor(Y)
    X = X + (row % 2) * stagger
    u = X - np.floor(X)
    v = Y - np.floor(Y)
    m = (smoothstep(0, mortar, u) * smoothstep(0, mortar, 1 - u) *
         smoothstep(0, mortar, v) * smoothstep(0, mortar, 1 - v))
    cell = (np.floor(X) * 131 + row * 71) % 97
    return m.astype(np.float32), u, v, cell.astype(np.float32)


def streaks(size, count=60, seed=0, length=0.55, width=0.004):
    """Vertical runoff stains — the single most useful decal for a wet ruin."""
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size), np.float32)
    xs = np.arange(size, dtype=np.float32)
    for _ in range(count):
        x0 = rng.random() * size
        top = rng.random() * size * 0.45
        ln = (0.25 + rng.random() * length) * size
        w = (width + rng.random() * width * 2.5) * size
        wob = rng.random() * 2.0
        ys = np.arange(size, dtype=np.float32)
        cx = x0 + np.sin(ys * 0.02 * wob) * w * 0.8
        d = np.abs(((xs[None, :] - cx[:, None] + size * 0.5) % size) - size * 0.5)
        prof = np.exp(-(d / w) ** 2)
        fade = np.clip((ys - top) / max(ln, 1e-3), 0, 1)
        fade = fade * np.clip(1.0 - (ys - top - ln) / (size * 0.2), 0, 1)
        out += prof * fade[:, None] * (0.4 + rng.random() * 0.6)
    return np.clip(out, 0, 1)


# ------------------------------------------------------------- conversion

def height_to_normal(h, strength=2.0):
    """Sobel gradients of the height field, packed to tangent-space RGB."""
    hx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    hy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.stack([-hx, -hy, np.ones_like(h)], -1)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    return (n * 0.5 + 0.5).astype(np.float32)


def height_to_ao(h, radius=6, strength=1.0):
    """Cheap cavity AO: how far below its local average a texel sits."""
    k = radius * 2 + 1
    pad = np.pad(h, radius, mode="wrap")
    acc = np.zeros_like(h)
    for dy in range(0, k, 2):
        for dx in range(0, k, 2):
            acc += pad[dy:dy + h.shape[0], dx:dx + h.shape[1]]
    n = len(range(0, k, 2)) ** 2
    local = acc / n
    ao = 1.0 - np.clip((local - h) * strength * 4.0, 0, 1)
    return np.clip(ao, 0, 1).astype(np.float32)


def save_rgb(path, rgb):
    a = np.clip(rgb, 0, 1)
    Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGB").save(
        path, optimize=True)


def save_gray_packed(path, r, g, b):
    a = np.clip(np.stack([r, g, b], -1), 0, 1)
    Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGB").save(
        path, optimize=True)


def tint(h, colour_lo, colour_hi):
    """Map a scalar field through a two-point colour ramp."""
    lo = np.array(colour_lo, np.float32)
    hi = np.array(colour_hi, np.float32)
    return lo[None, None, :] + (hi - lo)[None, None, :] * h[..., None]
