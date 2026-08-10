// mesh.h - CPU-side geometry building.
//
// Vertices carry a material index so an entire chunk of city - road, kerb,
// brick, window, corrugated steel - draws in a single call against a texture
// array, and a tangent so the baked normal maps have a frame to work in.
#pragma once
#include "hmath.h"
#include <vector>

namespace hm {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz;   // tangent, pointing along +U
    float u, v;
    float mat;          // layer index into the material texture array
    float ao;           // baked corner occlusion, 1 = open
};

struct Mesh {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    void clear() { verts.clear(); idx.clear(); }
    size_t triCount() const { return idx.size() / 3; }
    bool empty() const { return idx.empty(); }

    uint32_t push(const vec3& p, const vec3& n, const vec3& t,
                  float u, float v, float mat, float ao = 1.0f) {
        verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, u, v, mat, ao});
        return (uint32_t)(verts.size() - 1);
    }

    void tri(uint32_t a, uint32_t b, uint32_t c) {
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
    }

    // Quad with corners counter-clockwise seen from the front. The tangent is
    // derived from the a->b edge, which is the +U direction by construction.
    void quad(const vec3& a, const vec3& b, const vec3& c, const vec3& d,
              float mat, float uScale = 1.0f, float vScale = 1.0f,
              float ao0 = 1.0f, float ao1 = 1.0f, float ao2 = 1.0f, float ao3 = 1.0f) {
        vec3 n = normalize(cross(b - a, d - a));
        vec3 t = normalize(b - a);
        uint32_t i0 = push(a, n, t, 0, 0, mat, ao0);
        uint32_t i1 = push(b, n, t, uScale, 0, mat, ao1);
        uint32_t i2 = push(c, n, t, uScale, vScale, mat, ao2);
        uint32_t i3 = push(d, n, t, 0, vScale, mat, ao3);
        tri(i0, i1, i2);
        tri(i0, i2, i3);
    }

    // Axis-aligned box. uvPerMeter keeps texel density constant so a long wall
    // does not smear.
    void box(const vec3& mn, const vec3& mx, float mat, float uvPerMeter = 1.0f) {
        float sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
        quad({mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, mat, sz * uvPerMeter, sy * uvPerMeter);
        quad({mn.x, mn.y, mn.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, mat, sz * uvPerMeter, sy * uvPerMeter);
        quad({mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, mat, sx * uvPerMeter, sz * uvPerMeter);
        quad({mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, mat, sx * uvPerMeter, sz * uvPerMeter);
        quad({mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, mat, sx * uvPerMeter, sy * uvPerMeter);
        quad({mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, mat, sx * uvPerMeter, sy * uvPerMeter);
    }

    // Tapered vertical prism: the building block for limbs, poles and barrels.
    void taperedPrism(const vec3& base, float h, float rBottom, float rTop,
                      int sides, float mat) {
        int ring0 = (int)verts.size();
        for (int i = 0; i < sides; i++) {
            float a = (float)i / sides * TAU;
            float ca = std::cos(a), sa = std::sin(a);
            vec3 nb = normalize(vec3(ca, (rBottom - rTop) / std::max(h, 0.001f), sa));
            vec3 tg = normalize(vec3(-sa, 0, ca));
            push(base + vec3(ca * rBottom, 0, sa * rBottom), nb, tg,
                 (float)i / sides * 2.0f, 0.0f, mat);
        }
        int ring1 = (int)verts.size();
        for (int i = 0; i < sides; i++) {
            float a = (float)i / sides * TAU;
            float ca = std::cos(a), sa = std::sin(a);
            vec3 nb = normalize(vec3(ca, (rBottom - rTop) / std::max(h, 0.001f), sa));
            vec3 tg = normalize(vec3(-sa, 0, ca));
            push(base + vec3(ca * rTop, h, sa * rTop), nb, tg,
                 (float)i / sides * 2.0f, h, mat);
        }
        for (int i = 0; i < sides; i++) {
            int j = (i + 1) % sides;
            tri((uint32_t)(ring0 + i), (uint32_t)(ring0 + j), (uint32_t)(ring1 + j));
            tri((uint32_t)(ring0 + i), (uint32_t)(ring1 + j), (uint32_t)(ring1 + i));
        }
        if (rTop > 0.001f) {
            uint32_t c = push(base + vec3(0, h, 0), {0, 1, 0}, {1, 0, 0}, 0.5f, 0.5f, mat);
            for (int i = 0; i < sides; i++)
                tri(c, (uint32_t)(ring1 + i), (uint32_t)(ring1 + (i + 1) % sides));
        }
        if (rBottom > 0.001f) {
            uint32_t c = push(base, {0, -1, 0}, {1, 0, 0}, 0.5f, 0.5f, mat);
            for (int i = 0; i < sides; i++)
                tri(c, (uint32_t)(ring0 + (i + 1) % sides), (uint32_t)(ring0 + i));
        }
    }

    void ellipsoid(const vec3& c, const vec3& radii, int seg, int rings, float mat) {
        int base = (int)verts.size();
        for (int r = 0; r <= rings; r++) {
            float v = (float)r / rings;
            float phi = v * PI;
            for (int s = 0; s <= seg; s++) {
                float u = (float)s / seg;
                float theta = u * TAU;
                vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
                vec3 tg = normalize(vec3(-std::sin(theta), 0, std::cos(theta)));
                push(c + dir * radii, normalize(dir / radii), tg, u * 2.0f, v * 2.0f, mat);
            }
        }
        for (int r = 0; r < rings; r++) {
            for (int s = 0; s < seg; s++) {
                int i0 = base + r * (seg + 1) + s;
                int i1 = i0 + seg + 1;
                tri((uint32_t)i0, (uint32_t)(i0 + 1), (uint32_t)(i1 + 1));
                tri((uint32_t)i0, (uint32_t)(i1 + 1), (uint32_t)i1);
            }
        }
    }

    void append(const Mesh& o, const mat4& xf) {
        uint32_t base = (uint32_t)verts.size();
        for (const Vertex& v : o.verts) {
            vec3 p = xf.transformPoint({v.px, v.py, v.pz});
            vec3 n = normalize(xf.transformDir({v.nx, v.ny, v.nz}));
            vec3 t = normalize(xf.transformDir({v.tx, v.ty, v.tz}));
            verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z,
                             v.u, v.v, v.mat, v.ao});
        }
        for (uint32_t i : o.idx) idx.push_back(base + i);
    }

    // Override every vertex's material - lets one prop mesh be reused as wood,
    // metal or plastic without rebuilding it.
    void setMaterial(float mat) {
        for (Vertex& v : verts) v.mat = mat;
    }

    void bounds(vec3& mn, vec3& mx) const {
        if (verts.empty()) { mn = vec3(0, 0, 0); mx = vec3(0, 0, 0); return; }
        mn = vec3(1e30f, 1e30f, 1e30f);
        mx = vec3(-1e30f, -1e30f, -1e30f);
        for (const Vertex& v : verts) {
            mn = minv(mn, vec3(v.px, v.py, v.pz));
            mx = maxv(mx, vec3(v.px, v.py, v.pz));
        }
    }
};

} // namespace hm
