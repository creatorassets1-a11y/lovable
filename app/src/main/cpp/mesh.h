// mesh.h - CPU-side geometry building. Everything in this game is generated
// here at load time; there are no model files to ship or fail to load.
#pragma once
#include "hmath.h"
#include <vector>

namespace hm {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct Mesh {
    std::vector<Vertex> verts;
    std::vector<uint16_t> idx;

    void clear() { verts.clear(); idx.clear(); }
    size_t triCount() const { return idx.size() / 3; }

    uint16_t push(const vec3& p, const vec3& n, float u, float v) {
        verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, v});
        return (uint16_t)(verts.size() - 1);
    }

    void tri(uint16_t a, uint16_t b, uint16_t c) {
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
    }

    // Quad with corners in counter-clockwise order when viewed from the front.
    void quad(const vec3& a, const vec3& b, const vec3& c, const vec3& d,
              float uScale = 1.0f, float vScale = 1.0f) {
        vec3 n = normalize(cross(b - a, d - a));
        uint16_t i0 = push(a, n, 0, 0);
        uint16_t i1 = push(b, n, uScale, 0);
        uint16_t i2 = push(c, n, uScale, vScale);
        uint16_t i3 = push(d, n, 0, vScale);
        tri(i0, i1, i2);
        tri(i0, i2, i3);
    }

    // Axis-aligned box. `uvPerMeter` keeps texel density constant regardless
    // of how large the box is, so a long wall doesn't smear.
    void box(const vec3& mn, const vec3& mx, float uvPerMeter = 1.0f) {
        float sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
        // +X / -X
        quad({mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, sz * uvPerMeter, sy * uvPerMeter);
        quad({mn.x, mn.y, mn.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, sz * uvPerMeter, sy * uvPerMeter);
        // +Y / -Y
        quad({mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, sx * uvPerMeter, sz * uvPerMeter);
        quad({mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, sx * uvPerMeter, sz * uvPerMeter);
        // +Z / -Z
        quad({mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, sx * uvPerMeter, sy * uvPerMeter);
        quad({mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, sx * uvPerMeter, sy * uvPerMeter);
    }

    // Tapered vertical prism - the building block for limbs and torsos.
    // Radius shrinks from bottom to top, which is what makes the creature's
    // proportions read as wrong rather than merely blocky.
    void taperedPrism(const vec3& base, float h, float rBottom, float rTop, int sides) {
        int ring0 = (int)verts.size();
        for (int i = 0; i < sides; i++) {
            float a = (float)i / sides * TAU;
            float ca = std::cos(a), sa = std::sin(a);
            vec3 nb = normalize(vec3(ca, (rBottom - rTop) / std::max(h, 0.001f), sa));
            push(base + vec3(ca * rBottom, 0, sa * rBottom), nb, (float)i / sides * 2.0f, 0.0f);
        }
        int ring1 = (int)verts.size();
        for (int i = 0; i < sides; i++) {
            float a = (float)i / sides * TAU;
            float ca = std::cos(a), sa = std::sin(a);
            vec3 nb = normalize(vec3(ca, (rBottom - rTop) / std::max(h, 0.001f), sa));
            push(base + vec3(ca * rTop, h, sa * rTop), nb, (float)i / sides * 2.0f, h);
        }
        for (int i = 0; i < sides; i++) {
            int j = (i + 1) % sides;
            tri((uint16_t)(ring0 + i), (uint16_t)(ring0 + j), (uint16_t)(ring1 + j));
            tri((uint16_t)(ring0 + i), (uint16_t)(ring1 + j), (uint16_t)(ring1 + i));
        }
        // Cap the top so limbs aren't hollow when seen end-on.
        if (rTop > 0.001f) {
            uint16_t center = push(base + vec3(0, h, 0), {0, 1, 0}, 0.5f, 0.5f);
            for (int i = 0; i < sides; i++) {
                int j = (i + 1) % sides;
                tri(center, (uint16_t)(ring1 + i), (uint16_t)(ring1 + j));
            }
        }
        if (rBottom > 0.001f) {
            uint16_t center = push(base, {0, -1, 0}, 0.5f, 0.5f);
            for (int i = 0; i < sides; i++) {
                int j = (i + 1) % sides;
                tri(center, (uint16_t)(ring0 + j), (uint16_t)(ring0 + i));
            }
        }
    }

    // UV-sphere, optionally squashed per-axis. Used for heads and joints.
    void ellipsoid(const vec3& c, const vec3& radii, int seg, int rings) {
        int base = (int)verts.size();
        for (int r = 0; r <= rings; r++) {
            float v = (float)r / rings;
            float phi = v * PI;
            for (int s = 0; s <= seg; s++) {
                float u = (float)s / seg;
                float theta = u * TAU;
                vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
                push(c + dir * radii, normalize(dir / radii), u * 2.0f, v * 2.0f);
            }
        }
        for (int r = 0; r < rings; r++) {
            for (int s = 0; s < seg; s++) {
                int i0 = base + r * (seg + 1) + s;
                int i1 = i0 + seg + 1;
                tri((uint16_t)i0, (uint16_t)(i0 + 1), (uint16_t)(i1 + 1));
                tri((uint16_t)i0, (uint16_t)(i1 + 1), (uint16_t)i1);
            }
        }
    }

    // Append another mesh, transformed. Lets us bake props into the level mesh
    // so the whole world is one draw call.
    void append(const Mesh& o, const mat4& xf) {
        uint16_t base = (uint16_t)verts.size();
        for (const Vertex& v : o.verts) {
            vec3 p = xf.transformPoint({v.px, v.py, v.pz});
            vec3 n = normalize(xf.transformDir({v.nx, v.ny, v.nz}));
            verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, v.u, v.v});
        }
        for (uint16_t i : o.idx) idx.push_back((uint16_t)(base + i));
    }
};

} // namespace hm
