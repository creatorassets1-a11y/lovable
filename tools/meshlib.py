#!/usr/bin/env python3
"""Procedural 3D asset authoring.

Characters are defined as signed distance fields — capsules and ellipsoids
blended with a smooth minimum — and polygonised with marching cubes. That gets
organic, continuous shapes with no seams and no UV unwrapping problem: the
shader samples material by triplanar projection instead.

Normals come from the analytic SDF gradient rather than from face averaging, so
silhouettes stay smooth at low polygon counts.

Skinning is automatic: each vertex is weighted against the four nearest bone
segments by inverse distance. Animation clips are authored as sparse keyframes
of per-bone Euler rotations and baked to a fixed frame rate.

Output is .hmsh, a flat binary the engine can upload almost without parsing.
"""
import math
import struct

import numpy as np
from skimage import measure

MAGIC = b"HMS1"
FLAG_SKINNED = 1
MAX_BONE_NAME = 32


# ============================================================ SDF primitives
#
# Every primitive takes a point array p of shape (N, 3) and returns distances.

def sd_sphere(p, c, r):
    return np.linalg.norm(p - c, axis=-1) - r


def sd_ellipsoid(p, c, r):
    q = (p - c) / r
    k0 = np.linalg.norm(q, axis=-1)
    k1 = np.linalg.norm(q / r, axis=-1)
    return np.where(k0 > 1e-6, k0 * (k0 - 1.0) / np.maximum(k1, 1e-6), -min(r))


def sd_capsule(p, a, b, ra, rb=None):
    """Round cone between two points — the workhorse for limbs."""
    if rb is None:
        rb = ra
    a = np.asarray(a, np.float32)
    b = np.asarray(b, np.float32)
    ba = b - a
    ll = float(np.dot(ba, ba)) + 1e-9
    pa = p - a
    h = np.clip(np.einsum("ij,j->i", pa, ba) / ll, 0.0, 1.0)
    d = np.linalg.norm(pa - h[:, None] * ba, axis=-1)
    return d - (ra + (rb - ra) * h)


def sd_box(p, c, half, round_r=0.0):
    q = np.abs(p - c) - half
    outside = np.linalg.norm(np.maximum(q, 0.0), axis=-1)
    inside = np.minimum(np.max(q, axis=-1), 0.0)
    return outside + inside - round_r


def smin(a, b, k):
    """Polynomial smooth minimum. This is what makes joints look grown, not glued."""
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1 - h) + a * h - k * h * (1 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


class SDF:
    """Composable field. `union` blends, `cut` subtracts, `bump` displaces."""

    def __init__(self):
        self.ops = []

    def union(self, fn, k=0.0):
        self.ops.append(("u", fn, k))
        return self

    def cut(self, fn, k=0.0):
        self.ops.append(("c", fn, k))
        return self

    def bump(self, fn):
        self.ops.append(("b", fn, 0.0))
        return self

    def eval(self, p):
        d = None
        for kind, fn, k in self.ops:
            if kind == "b":
                d = d + fn(p)
                continue
            v = fn(p)
            if d is None:
                d = v
                continue
            if kind == "u":
                d = smin(d, v, k) if k > 0 else np.minimum(d, v)
            else:
                d = smax(d, -v, k) if k > 0 else np.maximum(d, -v)
        return d


def fbm3(p, octaves=4, freq=1.0, seed=0):
    """Cheap value-noise fbm over 3D points, for skin/cloth displacement."""
    total = np.zeros(len(p), np.float32)
    amp, f, norm = 0.5, freq, 0.0
    for o in range(octaves):
        q = p * f + seed * 13.7 + o * 4.1
        # Smooth pseudo-noise from summed sines: no lattice, no lookup table.
        n = (np.sin(q[:, 0] * 1.7 + np.cos(q[:, 1] * 2.3)) *
             np.sin(q[:, 1] * 1.9 + np.cos(q[:, 2] * 2.1)) *
             np.sin(q[:, 2] * 2.2 + np.cos(q[:, 0] * 1.6)))
        total += n * amp
        norm += amp
        amp *= 0.5
        f *= 2.07
    return (total / norm).astype(np.float32)


# ============================================================== polygonising

def polygonise(sdf, bounds_min, bounds_max, res=96):
    """Marching cubes over an SDF, returning (vertices, normals, indices).

    Normals are taken from the field gradient by central differences, which is
    both cheaper and smoother than averaging triangle normals.
    """
    bmin = np.asarray(bounds_min, np.float32)
    bmax = np.asarray(bounds_max, np.float32)
    size = bmax - bmin
    # Keep voxels roughly cubic so the mesh is not anisotropically faceted.
    dims = np.maximum(8, (size / size.max() * res).astype(int))

    xs = np.linspace(bmin[0], bmax[0], dims[0], dtype=np.float32)
    ys = np.linspace(bmin[1], bmax[1], dims[1], dtype=np.float32)
    zs = np.linspace(bmin[2], bmax[2], dims[2], dtype=np.float32)
    gx, gy, gz = np.meshgrid(xs, ys, zs, indexing="ij")
    pts = np.stack([gx.ravel(), gy.ravel(), gz.ravel()], axis=-1)

    field = sdf.eval(pts).reshape(dims).astype(np.float32)

    spacing = (size / np.maximum(dims - 1, 1)).astype(np.float32)
    verts, faces, _, _ = measure.marching_cubes(field, level=0.0, spacing=tuple(spacing))
    verts = verts.astype(np.float32) + bmin

    # Gradient normals via central differences on the field itself.
    eps = float(spacing.min()) * 0.75
    n = np.zeros_like(verts)
    for axis in range(3):
        off = np.zeros(3, np.float32)
        off[axis] = eps
        n[:, axis] = sdf.eval(verts + off) - sdf.eval(verts - off)
    ln = np.linalg.norm(n, axis=-1, keepdims=True)
    n = n / np.maximum(ln, 1e-8)

    return verts, n.astype(np.float32), faces.astype(np.uint32)


def weld(verts, normals, indices, tol=1e-4):
    """Merge coincident vertices so the index buffer is actually shared."""
    key = np.round(verts / tol).astype(np.int64)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    order = np.argsort(first)
    remap = np.zeros(len(first), np.int64)
    remap[order] = np.arange(len(first))
    new_idx = remap[inverse]

    out_v = np.zeros((len(first), 3), np.float32)
    out_n = np.zeros((len(first), 3), np.float32)
    np.add.at(out_v, new_idx, verts)
    np.add.at(out_n, new_idx, normals)
    counts = np.bincount(new_idx, minlength=len(first)).astype(np.float32)[:, None]
    out_v /= counts
    out_n /= np.maximum(np.linalg.norm(out_n, axis=-1, keepdims=True), 1e-8)
    return out_v, out_n.astype(np.float32), new_idx[indices].astype(np.uint32)


def decimate_grid(verts, normals, indices, cell):
    """Vertex-clustering decimation. Crude, fast, and adequate for LOD0 on a
    phone — the creature is rarely closer than two metres."""
    key = np.floor(verts / cell).astype(np.int64)
    _, inverse = np.unique(key, axis=0, return_inverse=True)
    nv = inverse.max() + 1
    out_v = np.zeros((nv, 3), np.float32)
    out_n = np.zeros((nv, 3), np.float32)
    np.add.at(out_v, inverse, verts)
    np.add.at(out_n, inverse, normals)
    counts = np.maximum(np.bincount(inverse, minlength=nv), 1).astype(np.float32)[:, None]
    out_v /= counts
    out_n /= np.maximum(np.linalg.norm(out_n, axis=-1, keepdims=True), 1e-8)

    tris = inverse[indices]
    good = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])
    return out_v, out_n.astype(np.float32), tris[good].astype(np.uint32)


# ================================================================== skeleton

class Bone:
    def __init__(self, name, head, tail, parent=-1):
        self.name = name
        self.head = np.asarray(head, np.float32)
        self.tail = np.asarray(tail, np.float32)
        self.parent = parent


class Skeleton:
    def __init__(self):
        self.bones = []
        self.index = {}

    def add(self, name, head, tail, parent=None):
        pi = -1 if parent is None else self.index[parent]
        self.index[name] = len(self.bones)
        self.bones.append(Bone(name, head, tail, pi))
        return name

    def __len__(self):
        return len(self.bones)

    def rest_local(self):
        """Local rest translation of each bone (head relative to parent head)."""
        out = []
        for b in self.bones:
            if b.parent < 0:
                out.append(b.head.copy())
            else:
                out.append(b.head - self.bones[b.parent].head)
        return np.array(out, np.float32)

    def inverse_bind(self):
        """Rest pose is translation-only, so the inverse bind is just -head.

        Returned already transposed: numpy writes row-major with .tobytes(),
        while the engine's mat4 is column-major (m[col*4 + row], matching GLSL).
        Without this the translation lands in the bottom row instead of the last
        column and every skinning matrix is silently garbage.
        """
        mats = []
        for b in self.bones:
            m = np.eye(4, dtype=np.float32)
            m[:3, 3] = -b.head
            mats.append(m.T.copy())
        return np.array(mats, np.float32)

    def skin(self, verts, falloff=2.6, max_infl=4):
        """Inverse-distance weights against the nearest bone segments."""
        nb = len(self.bones)
        d = np.zeros((len(verts), nb), np.float32)
        for i, b in enumerate(self.bones):
            ba = b.tail - b.head
            ll = float(np.dot(ba, ba)) + 1e-9
            pa = verts - b.head
            h = np.clip(np.einsum("ij,j->i", pa, ba) / ll, 0.0, 1.0)
            d[:, i] = np.linalg.norm(pa - h[:, None] * ba, axis=-1)

        w = 1.0 / np.power(d + 1e-3, falloff)
        top = np.argsort(-w, axis=1)[:, :max_infl]
        rows = np.arange(len(verts))[:, None]
        tw = w[rows, top]
        tw /= np.maximum(tw.sum(axis=1, keepdims=True), 1e-9)

        idx = top.astype(np.uint8)
        wq = np.clip(np.round(tw * 255), 0, 255).astype(np.uint8)
        # Fix rounding so weights sum to exactly 255.
        diff = 255 - wq.sum(axis=1).astype(np.int32)
        wq[:, 0] = np.clip(wq[:, 0].astype(np.int32) + diff, 0, 255).astype(np.uint8)
        return idx, wq


# ================================================================ animation

def euler_quat(rx, ry, rz):
    """XYZ Euler degrees to quaternion (x, y, z, w)."""
    hx, hy, hz = math.radians(rx) / 2, math.radians(ry) / 2, math.radians(rz) / 2
    cx, sx = math.cos(hx), math.sin(hx)
    cy, sy = math.cos(hy), math.sin(hy)
    cz, sz = math.cos(hz), math.sin(hz)
    return np.array([
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    ], np.float32)


def qslerp(a, b, t):
    d = float(np.dot(a, b))
    if d < 0:
        b = -b
        d = -d
    if d > 0.9995:
        r = a + (b - a) * t
        return r / np.linalg.norm(r)
    th0 = math.acos(max(-1.0, min(1.0, d)))
    th = th0 * t
    s0 = math.sin(th0 - th) / math.sin(th0)
    s1 = math.sin(th) / math.sin(th0)
    return a * s0 + b * s1


class Clip:
    """Sparse keyframes baked to a fixed rate at write time.

    keys: {bone_name: [(time, (rx, ry, rz), (dx, dy, dz)), ...]}
    """

    def __init__(self, name, duration, fps=30, loop=True):
        self.name = name
        self.duration = duration
        self.fps = fps
        self.loop = loop
        self.keys = {}

    def key(self, bone, t, rot=(0, 0, 0), pos=(0, 0, 0)):
        self.keys.setdefault(bone, []).append((float(t), rot, pos))
        return self

    def bake(self, skel):
        frames = max(2, int(round(self.duration * self.fps)) + 1)
        nb = len(skel)
        rest = skel.rest_local()
        rots = np.zeros((frames, nb, 4), np.float32)
        rots[:, :, 3] = 1.0
        poss = np.tile(rest[None, :, :], (frames, 1, 1)).astype(np.float32)

        for bname, keys in self.keys.items():
            if bname not in skel.index:
                continue
            bi = skel.index[bname]
            ks = sorted(keys, key=lambda k: k[0])
            times = [k[0] for k in ks]
            quats = [euler_quat(*k[1]) for k in ks]
            offs = [np.asarray(k[2], np.float32) for k in ks]
            for f in range(frames):
                t = f / self.fps
                if t <= times[0]:
                    q, o = quats[0], offs[0]
                elif t >= times[-1]:
                    q, o = quats[-1], offs[-1]
                else:
                    j = 0
                    while j < len(times) - 2 and times[j + 1] < t:
                        j += 1
                    span = max(1e-6, times[j + 1] - times[j])
                    u = (t - times[j]) / span
                    u = u * u * (3 - 2 * u)          # ease, so nothing snaps
                    q = qslerp(quats[j], quats[j + 1], u)
                    o = offs[j] + (offs[j + 1] - offs[j]) * u
                rots[f, bi] = q
                poss[f, bi] = rest[bi] + o
        return rots, poss


# =================================================================== writing

class Mesh:
    def __init__(self, name, verts, normals, indices, uvs=None,
                 bone_idx=None, bone_w=None, skel=None, clips=None):
        self.name = name
        self.verts = verts.astype(np.float32)
        self.normals = normals.astype(np.float32)
        # Always flat. Index arrays arrive as (T, 3) from marching cubes, and
        # len() on those counts triangles, not indices — which silently writes a
        # header three times too small.
        self.indices = np.asarray(indices, np.uint32).reshape(-1)
        n = len(verts)
        if uvs is None:
            # Triplanar in the shader means UVs are only a fallback; a cheap
            # cylindrical projection keeps decals and detail maps usable.
            ang = np.arctan2(self.verts[:, 2], self.verts[:, 0]) / (2 * math.pi) + 0.5
            uvs = np.stack([ang, self.verts[:, 1] * 0.5], axis=-1)
        self.uvs = uvs.astype(np.float32)
        self.bone_idx = bone_idx if bone_idx is not None else np.zeros((n, 4), np.uint8)
        self.bone_w = bone_w if bone_w is not None else np.tile(
            np.array([255, 0, 0, 0], np.uint8), (n, 1))
        self.skel = skel
        self.clips = clips or []

    @property
    def skinned(self):
        return self.skel is not None

    def tangents(self):
        """Per-vertex tangents from UV derivatives, orthonormalised."""
        tan = np.zeros((len(self.verts), 3), np.float32)
        tris = self.indices.reshape(-1, 3)
        p0, p1, p2 = (self.verts[tris[:, i]] for i in range(3))
        u0, u1, u2 = (self.uvs[tris[:, i]] for i in range(3))
        e1, e2 = p1 - p0, p2 - p0
        d1, d2 = u1 - u0, u2 - u0
        denom = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        r = np.where(np.abs(denom) < 1e-8, 0.0, 1.0 / np.where(denom == 0, 1, denom))
        t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r[:, None]
        for i in range(3):
            np.add.at(tan, tris[:, i], t)
        # Gram-Schmidt against the normal.
        dot = np.einsum("ij,ij->i", tan, self.normals)[:, None]
        tan = tan - self.normals * dot
        ln = np.linalg.norm(tan, axis=-1, keepdims=True)
        fallback = np.tile(np.array([1, 0, 0], np.float32), (len(tan), 1))
        tan = np.where(ln > 1e-6, tan / np.maximum(ln, 1e-8), fallback)
        return tan.astype(np.float32)

    def write(self, path):
        v = self.verts
        n = self.normals
        t = self.tangents()
        uv = self.uvs
        skinned = self.skinned

        bmin = v.min(axis=0)
        bmax = v.max(axis=0)

        with open(path, "wb") as f:
            f.write(MAGIC)
            f.write(struct.pack("<IIIII",
                                FLAG_SKINNED if skinned else 0,
                                len(v), len(self.indices),
                                len(self.skel) if skinned else 0,
                                len(self.clips)))
            f.write(struct.pack("<6f", *bmin, *bmax))

            # Interleaved vertex stream: pos, nrm, tan(+sign), uv, [idx, weight]
            stride_parts = [v, n, t, np.ones((len(v), 1), np.float32), uv]
            block = np.concatenate(stride_parts, axis=1).astype(np.float32)
            if skinned:
                f.write(block.tobytes())
                f.write(self.bone_idx.astype(np.uint8).tobytes())
                f.write(self.bone_w.astype(np.uint8).tobytes())
            else:
                f.write(block.tobytes())

            f.write(self.indices.astype(np.uint32).tobytes())

            if skinned:
                inv = self.skel.inverse_bind()
                for i, b in enumerate(self.skel.bones):
                    f.write(b.name.encode()[:MAX_BONE_NAME].ljust(MAX_BONE_NAME, b"\0"))
                    f.write(struct.pack("<i", b.parent))
                    f.write(inv[i].astype(np.float32).tobytes())

                for clip in self.clips:
                    rots, poss = clip.bake(self.skel)
                    f.write(clip.name.encode()[:MAX_BONE_NAME].ljust(MAX_BONE_NAME, b"\0"))
                    f.write(struct.pack("<fIfI", clip.duration, rots.shape[0],
                                        float(clip.fps), 1 if clip.loop else 0))
                    f.write(rots.astype(np.float32).tobytes())
                    f.write(poss.astype(np.float32).tobytes())

        return path


def report(mesh, path):
    import os
    print(f"  {mesh.name:16} {len(mesh.verts):6d} verts  "
          f"{len(mesh.indices)//3:6d} tris  "
          f"{'skinned ' + str(len(mesh.skel)) + ' bones' if mesh.skinned else 'static':16} "
          f"{os.path.getsize(path)/1024:8.1f} KB")
